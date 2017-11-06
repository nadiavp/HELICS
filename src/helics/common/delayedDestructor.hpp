/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#ifndef HELICS_DELAYED_DESTRUCTOR_HPP_
#define HELICS_DELAYED_DESTRUCTOR_HPP_
#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/** helper class to destroy objects at a late time when it is convenient and there are no more possibilities of
 * threading issues*/
template <class X>
class DelayedDestructor
{
  private:
    std::mutex destructionLock;
    std::vector<std::shared_ptr<X>> ElementsToBeDestroyed;

  public:
    DelayedDestructor () = default;
    ~DelayedDestructor ()
    {
        int ii = 0;
        while (!ElementsToBeDestroyed.empty ())
        {
            ++ii;
            destroyObjects ();
            if (!ElementsToBeDestroyed.empty ())
            {
                if (ii > 20)
                {
                    std::cerr << "error: unable to destroy all objects giving up\n";
                    break;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
        }
    }
    DelayedDestructor (DelayedDestructor &&) noexcept = delete;
    DelayedDestructor &operator= (DelayedDestructor &&) noexcept = delete;

    size_t destroyObjects ()
    {
        std::lock_guard<std::mutex> lock (destructionLock);
        if (!ElementsToBeDestroyed.empty ())
        {
            auto loc = std::remove_if (ElementsToBeDestroyed.begin (), ElementsToBeDestroyed.end (),
                                       [](const auto &element) { return (element.use_count () <= 1); });
            ElementsToBeDestroyed.erase (loc, ElementsToBeDestroyed.end ());
        }
        return ElementsToBeDestroyed.size ();
    }

    size_t destroyObjects (int delay)
    {
        std::unique_lock<std::mutex> lock (destructionLock);
        auto delayTime = std::chrono::milliseconds ((delay < 100) ? delay : 50);
        int delayCount = (delay < 100) ? 1 : (delay / 50);

        if (!ElementsToBeDestroyed.empty ())
        {
            auto loc = std::remove_if (ElementsToBeDestroyed.begin (), ElementsToBeDestroyed.end (),
                                       [](const auto &element) { return (element.use_count () <= 1); });
            ElementsToBeDestroyed.erase (loc, ElementsToBeDestroyed.end ());
            int cnt = 0;
            while ((!ElementsToBeDestroyed.empty ()) && (cnt < delayCount))
            {
                lock.unlock ();
                std::this_thread::sleep_for (delayTime);
                ++cnt;
                lock.lock ();
                if (!ElementsToBeDestroyed.empty ())
                {
                    loc = std::remove_if (ElementsToBeDestroyed.begin (), ElementsToBeDestroyed.end (),
                                          [](const auto &element) { return (element.use_count () <= 1); });
                    ElementsToBeDestroyed.erase (loc, ElementsToBeDestroyed.end ());
                }
            }
        }
        return ElementsToBeDestroyed.size ();
    }

    void addObjectsToBeDestroyed (std::shared_ptr<X> &&obj)
    {
        std::lock_guard<std::mutex> lock (destructionLock);
        ElementsToBeDestroyed.push_back (std::move (obj));
    }
    void addObjectsToBeDestroyed (std::shared_ptr<X> &obj)
    {
        std::lock_guard<std::mutex> lock (destructionLock);
        ElementsToBeDestroyed.push_back (obj);
    }
};
#endif  // HELICS_DELAYED_DESTRUCTOR_HPP_
