/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#include "helics/core/zmq/ZmqCore.h"
#include "helics/common/blocking_queue.h"
#include "helics/common/zmqContextManager.h"
#include "helics/common/zmqHelper.h"
#include "helics/common/zmqSocketDescriptor.h"
#include "helics/config.h"
#include "helics/core/core-data.h"
#include "helics/core/core.h"
#include "helics/core/helics-time.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>


#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#define USE_LOGGING 1
#if USE_LOGGING
#if HELICS_HAVE_GLOG
#include <glog/logging.h>
#define ENDL ""
#else
#define LOG(LEVEL) std::cout
#define ENDL std::endl
#endif
#else
#define LOG(LEVEL) std::ostringstream ()
#define ENDL std::endl
#endif

static const std::string DEFAULT_BROKER = "tcp://localhost";
constexpr int defaultBrokerREPport = 23405;
constexpr int defaultBrokerPULLport = 23406;


namespace helics
{
static void argumentParser (int argc, char *argv[], boost::program_options::variables_map &vm_map)
{
    namespace po = boost::program_options;
    po::options_description cmd_only ("command line only");
    po::options_description config ("configuration");
    po::options_description hidden ("hidden");

    // clang-format off
	// input boost controls
	cmd_only.add_options()
		("help,h", "produce help message")
		("config-file", po::value<std::string>(), "specify a configuration file to use");


	config.add_options()
		("broker,b", po::value<std::string>(), "identifier for the broker")
		("port", po::value<int>(),"port number for the broker")
		("register", "register the core for global locating");


    // clang-format on

    po::options_description cmd_line ("command line options");
    po::options_description config_file ("configuration file options");
    po::options_description visible ("allowed options");

    cmd_line.add (cmd_only).add (config);
    config_file.add (config);
    visible.add (cmd_only).add (config);

    // po::positional_options_description p;
    // p.add("input", -1);

    po::variables_map cmd_vm;
    try
    {
        po::store (po::command_line_parser (argc, argv).options (cmd_line).allow_unregistered ().run (), cmd_vm);
    }
    catch (std::exception &e)
    {
        std::cerr << e.what () << std::endl;
        throw (e);
    }

    po::notify (cmd_vm);

    // objects/pointers/variables/constants


    // program options control
    if (cmd_vm.count ("help") > 0)
    {
        std::cout << visible << '\n';
        return;
    }

    po::store (po::command_line_parser (argc, argv).options (cmd_line).allow_unregistered ().run (), vm_map);

    if (cmd_vm.count ("config-file") > 0)
    {
        std::string config_file_name = cmd_vm["config-file"].as<std::string> ();
        if (!boost::filesystem::exists (config_file_name))
        {
            std::cerr << "config file " << config_file_name << " does not exist\n";
            throw (std::invalid_argument ("unknown config file"));
        }
        else
        {
            std::ifstream fstr (config_file_name.c_str ());
            po::store (po::parse_config_file (fstr, config_file), vm_map);
            fstr.close ();
        }
    }

    po::notify (vm_map);
}


ZmqCore::ZmqCore (const std::string &core_name) : CommonCore (core_name) {}

void ZmqCore::initializeFromArgs (int argc, char *argv[])
{
    namespace po = boost::program_options;
    if (coreState == created)
    {
        po::variables_map vm;
        argumentParser (argc, argv, vm);

        if (vm.count ("broker") > 0)
        {
            auto brstring = vm["broker"].as<std::string> ();
            // tbroker = findTestBroker(brstring);
        }

        if (vm.count ("brokerinit") > 0)
        {
            // tbroker->Initialize(vm["brokerinit"].as<std::string>());
        }
        CommonCore::initializeFromArgs (argc, argv);
    }
}

bool ZmqCore::brokerConnect () { return true; }
#define NEW_ROUTE 233
#define DISCONNECT 2523

void ZmqCore::brokerDisconnect ()
{
    ActionMessage rt (CMD_PROTOCOL);
    rt.index = DISCONNECT;
    transmit (-1, rt);
}

void ZmqCore::transmit (int route_id, const ActionMessage &cmd)
{
    txQueue.push (std::pair<int, ActionMessage> (route_id, cmd));
}


void ZmqCore::addRoute (int route_id, const std::string &routeInfo)
{
    ActionMessage rt (CMD_PROTOCOL);
    rt.payload = routeInfo;
    rt.index = NEW_ROUTE;
    rt.source_id = route_id;
    transmit (-1, rt);
}


std::string ZmqCore::getAddress () const { return pullSocketAddress; }

void ZmqCore::transmitData ()
{
    auto ctx = zmqContextManager::getContextPointer ();
    zmq::socket_t reqSocket (ctx->getContext (), ZMQ_REQ);
    reqSocket.connect (brokerRepAddress.c_str ());
    zmq::socket_t brokerPushSocket (ctx->getContext (), ZMQ_PUSH);
    std::map<int, zmq::socket_t> pushSockets;  // for all the other possible routes
    zmq::socket_t controlSocket (ctx->getContext (), ZMQ_PAIR);

    std::string controlsockString = "inproc://" + getIdentifier () + "_control";
    controlSocket.bind (controlsockString.c_str ());
    // the receiver thread that is managed by this thread
	std::thread rxThread;
    std::vector<char> buffer;
    std::vector<char> rxbuffer (4096);
    while (1)
    {
        int route_id;
        ActionMessage cmd;
        std::tie (route_id, cmd) = txQueue.pop ();
        if (cmd.action () == CMD_PROTOCOL)
        {
            if (route_id == -1)
            {
                // do something local
            }
        }
        cmd.to_vector (buffer);
        if (isPriorityCommand (cmd))
        {
            reqSocket.send (buffer.data (), buffer.size ());

            // TODO:: need to figure out how to catch overflow and resize the rxbuffer
            // admittedly this would probably be a very very long name but it could happen
            auto nsize = reqSocket.recv (rxbuffer.data (), rxbuffer.size ());
			if ((nsize > 0) && (nsize < rxbuffer.size()))
			{
				ActionMessage rxcmd(rxbuffer.data(), rxbuffer.size());
				addCommand(rxcmd);
			}
            
        }
        if (route_id == 0)
        {
            brokerPushSocket.send (buffer.data (), buffer.size ());
        }
        else
        {
            auto rt_find = pushSockets.find (route_id);
            if (rt_find != pushSockets.end ())
            {
                rt_find->second.send (buffer.data (), buffer.size ());
            }
            else
            {
                brokerPushSocket.send (buffer.data (), buffer.size ());
            }
        }
    }
    reqSocket.close ();
    brokerPushSocket.close ();
    pushSockets.clear ();
	controlSocket.send("close");
	controlSocket.close();
	rxThread.join();
}

void ZmqCore::receiveData ()
{
    auto ctx = zmqContextManager::getContextPointer ();
    zmq::socket_t pullSocket (ctx->getContext (), ZMQ_PULL);

    pullSocket.bind (pullSocketAddress.c_str ());
    zmq::socket_t controlSocket (ctx->getContext (), ZMQ_PAIR);
    std::string controlsockString = "inproc://" + getIdentifier () + "_control";
    controlSocket.connect (controlsockString.c_str ());

	zmq::socket_t repSocket(ctx->getContext(), ZMQ_REP);
	repSocket.bind(brokerRepAddress.c_str());
	std::vector<zmq::pollitem_t> poller(3);
	poller[0].socket = static_cast<void *>(controlSocket);
	poller[0].events= ZMQ_POLLIN;
	poller[1].socket = static_cast<void *>(pullSocket);
	poller[1].events = ZMQ_POLLIN;
	poller[2].socket = static_cast<void *>(repSocket);
	poller[2].events = ZMQ_POLLIN;
	while (1)
	{
		auto rc = zmq::poll(poller);
		if (rc > 0)
		{
			if ((poller[0].revents&ZMQ_POLLIN) != 0)
			{
				zmq::message_t msg;
				controlSocket.recv(&msg);
				if (msg.size() == 5)
				{
					if (std::string(static_cast<char *>(msg.data()), msg.size()) == "close")
					{
						break;
					}
				}
			}
			if ((poller[1].revents&ZMQ_POLLIN) != 0)
			{
				zmq::message_t msg;
				pullSocket.recv(&msg);
				ActionMessage M(static_cast<char *>(msg.data()), msg.size());
				addCommand(std::move(M));
			}
			if ((poller[2].revents&ZMQ_POLLIN) != 0)
			{
				zmq::message_t msg;
				repSocket.recv(&msg);
				ActionMessage M(static_cast<char *>(msg.data()), msg.size());
				addCommand(M);
				ActionMessage resp(CMD_PRIORITY_ACK);
				auto str = resp.to_string();
				
				repSocket.send(str.data(), str.size());
			}
		}
	}
}

}  // namespace helics
