/*
Copyright © 2017-2019,
Battelle Memorial Institute; Lawrence Livermore National Security, LLC; Alliance for Sustainable Energy, LLC
All rights reserved. See LICENSE file and DISCLAIMER for more details.
*/
#pragma once

#include "../NetworkCore.hpp"

namespace helics
{
namespace ipc
{
class IpcComms;
/** implementation for the core that uses an ipc queue to communicate*/
using IpcCore = NetworkCore<IpcComms, interface_type::ipc>;

}  // namespace ipc
}  // namespace helics
