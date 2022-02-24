#pragma once

#include <FEXCore/Utils/CompilerDefs.h>

#include <sys/epoll.h>
#include <type_traits>

namespace FEX::HLE {
using key_serial_t = int32_t;
using kernel_timer_t = int32_t;
using mqd_t = int32_t;

#ifndef GETPID
#define GETPID 11
#endif

#ifndef GETVAL
#define GETVAL 12
#endif

#ifndef GETALL
#define GETALL 13
#endif

#ifndef GETNCNT
#define GETNCNT 14
#endif

#ifndef GETZCNT
#define GETZCNT 15
#endif

#ifndef SETVAL
#define SETVAL 16
#endif

#ifndef SETALL
#define SETALL 17
#endif

#ifndef SEM_STAT
#define SEM_STAT 18
#endif

#ifndef SEM_INFO
#define SEM_INFO 19
#endif

#ifndef SEM_STAT_ANY
#define SEM_STAT_ANY 20
#endif

struct FEX_PACKED epoll_event_x86 {
  uint32_t events;
  epoll_data_t data;

  epoll_event_x86() = delete;

  operator struct epoll_event() const {
    epoll_event event{};
    event.events = events;
    event.data = data;
    return event;
  }

  epoll_event_x86(struct epoll_event event) {
    events = event.events;
    data = event.data;
  }
};
static_assert(std::is_trivial<epoll_event_x86>::value, "Needs to be trivial");
static_assert(sizeof(epoll_event_x86) == 12, "Incorrect size");

}
