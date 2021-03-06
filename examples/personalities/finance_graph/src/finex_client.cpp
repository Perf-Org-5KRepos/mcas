#include <stdio.h>
#include <string>
#include <sstream>
#include <chrono>
#include <iostream>
#include <fstream>
#include <common/str_utils.h>
#include <common/dump_utils.h>
#include <common/cycles.h>
#include <boost/program_options.hpp>
#include <api/components.h>
#include <api/mcas_itf.h>
#include "finex_proto_generated.h"
#include "finex_types.h"

using namespace Graph_ADO_protocol;
using namespace flatbuffers;
using namespace Component;


struct Options
{
  unsigned debug_level;
  std::string server;
  std::string device;
  std::string data_dir;
  unsigned port;
} g_options;

Component::IMCAS* init(const std::string& server_hostname,  int port);

void do_load(Component::IMCAS* mcas);


int main(int argc, char * argv[])
{
  namespace po = boost::program_options;

  try {
    po::options_description desc("Options");

    desc.add_options()("help", "Show help")
      ("server", po::value<std::string>()->default_value("10.0.0.21"), "Server hostname")
      ("device", po::value<std::string>()->default_value("mlx5_0"), "Device (e.g. mlnx5_0)")
      ("port", po::value<unsigned>()->default_value(11911), "Server port")
      ("debug", po::value<unsigned>()->default_value(0), "Debug level")
      ("datadir", po::value<std::string>(), "Location of graph data")
      ("action", po::value<std::string>()->default_value("load"))
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help") > 0) {
      std::cout << desc;
      return -1;
    }

    if (vm.count("server") == 0) {
      std::cout << "--server option is required\n";
      return -1;
    }

    if (vm.count("datadir") == 0) {
      std::cout << "--datadir option is required\n";
      return -1;
    }

    g_options.server = vm["server"].as<std::string>();
    g_options.device = vm["device"].as<std::string>();
    g_options.port = vm["port"].as<unsigned>();
    g_options.debug_level = vm["debug"].as<unsigned>();
    g_options.data_dir = vm["datadir"].as<std::string>();

    auto mcasptr = init(vm["server"].as<std::string>(), vm["port"].as<unsigned>());

    if(vm["action"].as<std::string>() == "load") {
      do_load(mcasptr);
    }

    mcasptr->release_ref();
  }
  catch (po::error &) {
    printf("bad command line option\n");
    return -1;
  }
 
  return 0;
}

void load_transactions(Component::IMCAS* mcas, Component::IKVStore::pool_t pool)
{
  std::ifstream ifs(g_options.data_dir.c_str() + std::string("nodes.transactions.client-sourcing.csv"));
  if(!ifs.is_open())
    throw General_exception("unable to open nodes.clients.csv");

  FlatBufferBuilder fbb;
  std::string line;
  unsigned count = 0;
  
  while(getline(ifs, line)) {
    fbb.Clear();
    std::stringstream ss(line);
    std::string id, source, target, date, time, amount, currency;
    
    getline(ss, id, '|');
    getline(ss, source, '|');
    getline(ss, target, '|');
    getline(ss, date, '|');
    getline(ss, time, '|');
    getline(ss, amount, '|');
    getline(ss, currency, '|');


    auto record = CreateTransactionDirect(fbb,
                                          source.c_str(),
                                          target.c_str(),
                                          date.c_str(),
                                          time.c_str(),
                                          std::stof(amount),
                                          currency.c_str());

    auto msg = CreateMessage(fbb, Element_Transaction, record.Union());
    fbb.Finish(msg);

    //    hexdump(fbb.GetBufferPointer(), fbb.GetSize());
    
    std::vector<Component::IMCAS::ADO_response> response;
    
    status_t rc = mcas->invoke_ado(pool,
                                   id, /* key */
                                   fbb.GetBufferPointer(),
                                   fbb.GetSize(),
                                   IMCAS::ADO_FLAG_CREATE_ON_DEMAND,
                                   response,
                                   sizeof(finex::Transaction) /* size to reserve */);

    if(rc != S_OK)
      throw General_exception("failed to put transaction (%d)", rc);

    count++;
  }
  PINF("** Loaded transactions OK! (pool count=%lu)", mcas->count(pool));;
  
}

void load_clients(Component::IMCAS* mcas, Component::IKVStore::pool_t pool)
{
  std::ifstream ifs(g_options.data_dir.c_str() + std::string("nodes.clients.csv"));
  if(!ifs.is_open())
    throw General_exception("unable to open nodes.clients.csv");

  // skip header
  std::string line;
  getline(ifs, line);
  
  FlatBufferBuilder fbb;
  while(getline(ifs, line)) {
    fbb.Clear();
    std::stringstream ss(line);
    std::string field[14];
    for(unsigned i=0;i<14;i++)
      getline(ss, field[i], '|');

    auto record = CreateClientRecordDirect(fbb,
                                           field[1].c_str(),
                                           field[2].c_str(),
                                           std::stoi(field[3]),
                                           field[4].c_str(),
                                           field[5].c_str(),
                                           field[6].c_str(),
                                           field[7].c_str(),
                                           field[8].c_str(),
                                           field[9].c_str(),
                                           field[10].c_str(),
                                           field[11].c_str(),
                                           field[12].c_str(),
                                           field[13].c_str());

    auto msg = CreateMessage(fbb, Element_ClientRecord, record.Union());
    fbb.Finish(msg);

    //    std::string id = "records.clients." + field[0];
    std::string id = field[0];
    status_t rc = mcas->put(pool,
                            id,
                            fbb.GetBufferPointer(),
                            fbb.GetSize());
    if(rc != S_OK)
      throw General_exception("failed to put");      
  }
  PINF("** Loaded Clients OK! (total count=%lu)", mcas->count(pool));;
}


void load_atms(Component::IMCAS* mcas, Component::IKVStore::pool_t pool)
{
  std::ifstream ifs(g_options.data_dir.c_str() + std::string("nodes.atms.csv"));
  if(!ifs.is_open())
    throw General_exception("unable to open nodes.atms.csv");
  // skip header
  std::string line;
  getline(ifs, line);
  
  FlatBufferBuilder fbb;
  while(getline(ifs, line)) {
    fbb.Clear();
    std::stringstream ss(line);
    std::string id, longitude, latitude;
    getline(ss, id, '|');
    getline(ss, longitude, '|');
    getline(ss, latitude, '|');

    auto record = CreateAtmRecord(fbb,
                                  std::stof(longitude),
                                  std::stof(latitude));
    
    auto msg = CreateMessage(fbb, Element_AtmRecord, record.Union());
    fbb.Finish(msg);

    //    id = "records.atms." + id;
    status_t rc = mcas->put(pool,
                            id,
                            fbb.GetBufferPointer(),
                            fbb.GetSize());
    if(rc != S_OK)
      throw General_exception("failed to put");      
  }
  PINF("** Loaded ATMs OK! (total count=%lu)", mcas->count(pool));;
}

void load_companies(Component::IMCAS* mcas, Component::IKVStore::pool_t pool)
{
  // load nodes.companies.csv
  std::ifstream ifs(g_options.data_dir.c_str() + std::string("nodes.companies.csv"));
  if(!ifs.is_open())
    throw General_exception("unable to open nodes.companies.csv");

  // skip line
  std::string line;
  std::getline(ifs, line);
  {
    FlatBufferBuilder fbb;
    while(getline(ifs, line)) {
      fbb.Clear();
      std::stringstream ss(line);
      std::string id,type,name,country;
      getline(ss, id, '|');
      getline(ss, type, '|');
      getline(ss, name, '|');
      getline(ss, country, '|');

      /* convert to flatbuffer record */
      auto record = CreateCompanyRecordDirect(fbb,
                                              type.c_str(),
                                              name.c_str(),
                                              country.c_str());

      auto msg = CreateMessage(fbb, Element_CompanyRecord, record.Union());
      fbb.Finish(msg);

      //      id = "records.company." + id;
      status_t rc = mcas->put(pool,
                              id,
                              fbb.GetBufferPointer(),
                              fbb.GetSize());
      if(rc != S_OK)
        throw General_exception("failed to put");
      PLOG("put company: %s", name.c_str());
    }
  }
  PINF("** Loaded companies OK! (total count=%lu)", mcas->count(pool));;
}

void do_load(Component::IMCAS* mcas)
{
  PLOG("Loading data from: (%s)", g_options.data_dir.c_str());

  const std::string poolname = "finance-data-pool";  
  auto pool = mcas->create_pool(poolname,
                                GB(1),
                                0, /* flags */
                                1000000); /* obj count */
  if(pool == Component::IKVStore::POOL_ERROR)
    throw General_exception("create_pool (%s) failed", poolname.c_str());

  mcas->configure_pool(pool, "AddIndex::VolatileTree");

  load_companies(mcas, pool);
  load_atms(mcas, pool);
  load_clients(mcas, pool);
  load_transactions(mcas, pool);
  
#ifdef CHECK
  offset_t matched;
  std::string matched_key;
  status_t rc = mcas->find(pool, "regex:records.*", 0,
                           matched, matched_key);
  PLOG("found key:%s", matched_key.c_str());
#endif
  
  mcas->close_pool(pool);
}


Component::IMCAS * init(const std::string& server_hostname,  int port)
{
  using namespace Component;
  
  IBase *comp = Component::load_component("libcomponent-mcasclient.so",
                                          mcas_client_factory);

  auto fact = (IMCAS_factory *) comp->query_interface(IMCAS_factory::iid());
  if(!fact)
    throw Logic_exception("unable to create MCAS factory");

  std::stringstream url;
  url << g_options.server << ":" << g_options.port;
  
  IMCAS * mcas = fact->mcas_create(g_options.debug_level,
                                   "None",
                                   url.str(),
                                   g_options.device);

  if(!mcas)
    throw Logic_exception("unable to create MCAS client instance");

  fact->release_ref();

  return mcas;
}

