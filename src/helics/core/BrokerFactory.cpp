/*
Copyright © 2017-2018,
Battelle Memorial Institute; Lawrence Livermore National Security, LLC; Alliance for Sustainable Energy, LLC
All rights reserved. See LICENSE file and DISCLAIMER for more details.
*/
#include "BrokerFactory.hpp"
#include "../common/TripWire.hpp"
#include "../common/delayedDestructor.hpp"
#include "../common/searchableObjectHolder.hpp"
#include "core-exceptions.hpp"
#include "core-types.hpp"
#include "helics/helics-config.h"
#if HELICS_HAVE_ZEROMQ
#include "zmq/ZmqBroker.h"
#endif

#if HELICS_HAVE_MPI
#include "mpi/MpiBroker.h"
#endif

#ifndef DISABLE_TEST_CORE
#include "test/TestBroker.h"
#endif

#ifndef DISABLE_IPC_CORE
#include "ipc/IpcBroker.h"
#endif

#ifndef DISABLE_UDP_CORE
#include "udp/UdpBroker.h"
#endif

#ifndef DISABLE_TCP_CORE
#include "tcp/TcpBroker.h"
#endif

#include <cassert>

namespace helics
{
std::shared_ptr<Broker> makeBroker (core_type type, const std::string &name)
{
    std::shared_ptr<Broker> broker;

    if (type == core_type::DEFAULT)
    {
#if HELICS_HAVE_ZEROMQ
        type = core_type::ZMQ;
#else
#ifndef DISABLE_TCP_CORE
        type = core_type::TCP;
#else
        type = core_type::UDP;
#endif
#endif
    }

    switch (type)
    {
    case core_type::ZMQ:
#if HELICS_HAVE_ZEROMQ
        if (name.empty ())
        {
            broker = std::make_shared<zeromq::ZmqBroker> ();
        }
        else
        {
            broker = std::make_shared<zeromq::ZmqBroker> (name);
        }

#else
        throw (HelicsException ("ZMQ broker type is not available"));
#endif
        break;
    case core_type::ZMQ_SS:
#if HELICS_HAVE_ZEROMQ
        if (name.empty ())
        {
            broker = std::make_shared<zeromq::ZmqBrokerSS> ();
        }
        else
        {
            broker = std::make_shared<zeromq::ZmqBrokerSS> (name);
        }
#else
        throw (HelicsException ("ZMQ broker type is not available"));
#endif
        break;
    case core_type::MPI:
#if HELICS_HAVE_MPI
        if (name.empty ())
        {
            broker = std::make_shared<mpi::MpiBroker> ();
        }
        else
        {
            broker = std::make_shared<mpi::MpiBroker> (name);
        }
#else
        throw (HelicsException ("mpi broker type is not available"));
#endif
        break;
    case core_type::TEST:
#ifndef DISABLE_TEST_CORE
        if (name.empty ())
        {
            broker = std::make_shared<testcore::TestBroker> ();
        }
        else
        {
            broker = std::make_shared<testcore::TestBroker> (name);
        }
        break;
#else
        throw (HelicsException ("Test broker type is not available"));
#endif
    case core_type::INTERPROCESS:
    case core_type::IPC:
#ifndef DISABLE_IPC_CORE
        if (name.empty ())
        {
            broker = std::make_shared<ipc::IpcBroker> ();
        }
        else
        {
            broker = std::make_shared<ipc::IpcBroker> (name);
        }
        break;
#else
        throw (HelicsException ("ipc broker type is not available"));
#endif
    case core_type::UDP:
#ifndef DISABLE_UDP_CORE
        if (name.empty ())
        {
            broker = std::make_shared<udp::UdpBroker> ();
        }
        else
        {
            broker = std::make_shared<udp::UdpBroker> (name);
        }
        break;
#else
        throw (HelicsException ("udp broker type is not available"));
#endif
    case core_type::TCP:
#ifndef DISABLE_TCP_CORE
        if (name.empty ())
        {
            broker = std::make_shared<tcp::TcpBroker> ();
        }
        else
        {
            broker = std::make_shared<tcp::TcpBroker> (name);
        }
#else
        throw (HelicsException ("tcp broker type is not available"));
#endif
        break;
    case core_type::TCP_SS:
#ifndef DISABLE_TCP_CORE
        if (name.empty ())
        {
            broker = std::make_shared<tcp::TcpBrokerSS> ();
        }
        else
        {
            broker = std::make_shared<tcp::TcpBrokerSS> (name);
        }
#else
        throw (HelicsException ("tcp single socket broker type is not available"));
#endif
        break;
    default:
        throw (HelicsException ("unrecognized broker type"));
    }
    return broker;
}

namespace BrokerFactory
{
std::shared_ptr<Broker> create (core_type type, const std::string &initializationString)
{
    auto broker = makeBroker (type, std::string ());
    broker->initialize (initializationString);
    bool reg = registerBroker (broker);
    if (!reg)
    {
        throw (helics::RegistrationFailure ("unable to register broker"));
    }
    broker->connect ();
    return broker;
}

std::shared_ptr<Broker>
create (core_type type, const std::string &broker_name, const std::string &initializationString)
{
    auto broker = makeBroker (type, broker_name);
    broker->initialize (initializationString);
    bool reg = registerBroker (broker);
    if (!reg)
    {
        throw (helics::RegistrationFailure ("unable to register broker"));
    }
    broker->connect ();
    return broker;
}

std::shared_ptr<Broker> create (core_type type, int argc, const char *const *argv)
{
    auto broker = makeBroker (type, "");
    broker->initializeFromArgs (argc, argv);
    bool reg = registerBroker (broker);
    if (!reg)
    {
        throw (helics::RegistrationFailure ("unable to register broker"));
    }
    broker->connect ();
    return broker;
}

std::shared_ptr<Broker> create (core_type type, const std::string &broker_name, int argc, const char *const *argv)
{
    auto broker = makeBroker (type, broker_name);
    broker->initializeFromArgs (argc, argv);
    bool reg = registerBroker (broker);
    if (!reg)
    {
        throw (helics::RegistrationFailure ("unable to register broker"));
    }
    broker->connect ();
    return broker;
}

/** lambda function to join cores before the destruction happens to avoid potential problematic calls in the
 * loops*/
static auto destroyerCallFirst = [](auto &broker) {
    broker->processDisconnect (
      true);  // use true here as it is possible the searchableObjectHolder is deleted already
    broker->joinAllThreads ();
};
/** so the problem this is addressing is that unregister can potentially cause a destructor to fire
that destructor can delete a thread variable, unfortunately it is possible that a thread stored in this variable
can do the unregister operation and destroy itself meaning it is unable to join and thus will call std::terminate
what we do is delay the destruction until it is called in a different thread which allows the destructor to fire if
need be without issue*/

static DelayedDestructor<CoreBroker>
  delayedDestroyer (destroyerCallFirst);  //!< the object handling the delayed destruction

static SearchableObjectHolder<CoreBroker> searchableObjects;  //!< the object managing the searchable objects

// this will trip the line when it is destroyed at global destruction time
static tripwire::TripWireTrigger tripTrigger;

std::shared_ptr<Broker> findBroker (const std::string &brokerName)
{
    return searchableObjects.findObject (brokerName);
}

static bool isJoinableBrokerOfType (core_type type, const std::shared_ptr<Broker> &ptr)
{
    if (ptr->isOpenToNewFederates ())
    {
        switch (type)
        {
        case core_type::ZMQ:
#if HELICS_HAVE_ZEROMQ
            return (dynamic_cast<zeromq::ZmqBroker *> (ptr.get ()) != nullptr);
#else
            break;
#endif
        case core_type::MPI:
#if HELICS_HAVE_MPI
            return (dynamic_cast<mpi::MpiBroker *> (ptr.get ()) != nullptr);
#else
            break;
#endif
        case core_type::TEST:
#ifndef DISABLE_TEST_CORE
            return (dynamic_cast<testcore::TestBroker *> (ptr.get ()) != nullptr);
#else
            return false;
#endif
        case core_type::INTERPROCESS:
        case core_type::IPC:
#ifndef DISABLE_IPC_CORE
            return (dynamic_cast<ipc::IpcBroker *> (ptr.get ()) != nullptr);
#else
            return false;
#endif
        case core_type::UDP:
#ifndef DISABLE_UDP_CORE
            return (dynamic_cast<udp::UdpBroker *> (ptr.get ()) != nullptr);
#else
            return false;
#endif
        case core_type::TCP:
#ifndef DISABLE_TCP_CORE
            return (dynamic_cast<tcp::TcpBroker *> (ptr.get ()) != nullptr);
#else
            return false;
#endif
        default:
            return true;
        }
    }
    return false;
}

std::shared_ptr<Broker> findJoinableBrokerOfType (core_type type)
{
    return searchableObjects.findObject ([type](auto &ptr) { return isJoinableBrokerOfType (type, ptr); });
}

bool registerBroker (const std::shared_ptr<Broker> &broker)
{
    bool registered = false;
    auto tbroker = std::dynamic_pointer_cast<CoreBroker> (broker);
    if (tbroker)
    {
        registered = searchableObjects.addObject (tbroker->getIdentifier (), tbroker);
    }
    cleanUpBrokers ();
    if (!registered)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        registered = searchableObjects.addObject (tbroker->getIdentifier (), tbroker);
    }
    if (registered)
    {
        delayedDestroyer.addObjectsToBeDestroyed (tbroker);
    }

    return registered;
}

size_t cleanUpBrokers () { return delayedDestroyer.destroyObjects (); }
size_t cleanUpBrokers (std::chrono::milliseconds delay) { return delayedDestroyer.destroyObjects (delay); }

bool copyBrokerIdentifier (const std::string &copyFromName, const std::string &copyToName)
{
    return searchableObjects.copyObject (copyFromName, copyToName);
}

void unregisterBroker (const std::string &name)
{
    if (!searchableObjects.removeObject (name))
    {
        searchableObjects.removeObject ([&name](auto &obj) { return (obj->getIdentifier () == name); });
    }
}

void displayHelp (core_type type)
{
    switch (type)
    {
    case core_type::ZMQ:
#if HELICS_HAVE_ZEROMQ
        zeromq::ZmqBroker::displayHelp (true);
#endif
        break;
    case core_type::MPI:
#if HELICS_HAVE_MPI
        mpi::MpiBroker::displayHelp (true);
#endif
        break;
    case core_type::TEST:
#ifndef DISABLE_TEST_CORE
        testcore::TestBroker::displayHelp (true);
#endif
        break;
    case core_type::INTERPROCESS:
    case core_type::IPC:
#ifndef DISABLE_IPC_CORE
        ipc::IpcBroker::displayHelp (true);
#endif
        break;
    case core_type::TCP:
#ifndef DISABLE_TCP_CORE
        tcp::TcpBroker::displayHelp (true);
#endif
        break;
    case core_type::UDP:
#ifndef DISABLE_UDP_CORE
        udp::UdpBroker::displayHelp (true);
#endif
        break;
    default:
#if HELICS_HAVE_ZEROMQ
        zeromq::ZmqBroker::displayHelp (true);
#endif
#if HELICS_HAVE_MPI
        mpi::MpiBroker::displayHelp (true);
#endif
#ifndef DISABLE_IPC_CORE
        ipc::IpcBroker::displayHelp (true);
#endif
#ifndef DISABLE_TEST_CORE
        testcore::TestBroker::displayHelp (true);
#endif
#ifndef DISABLE_TCP_CORE
        tcp::TcpBroker::displayHelp (true);
#endif
#ifndef DISABLE_UDP_CORE
        udp::UdpBroker::displayHelp (true);
#endif
        break;
    }

    CoreBroker::displayHelp ();
}

}  // namespace BrokerFactory
}  // namespace helics
