/******************************************************************************
    Copyright � 2012-2015 Martin Karsten

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#ifndef _Scheduler_h_
#define _Scheduler_h_ 1

#include "generic/EmbeddedContainers.h"
#include "generic/tree.h"
#include "runtime/Runtime.h"

class Thread;
class ThreadNode;

//Declaring function
extern "C" void setSchedParameters(mword mingranularity, mword epochlen);

class Scheduler {
  friend void Runtime::idleLoop(Scheduler*);
  bufptr_t idleStack[minimumStack];
  int totalPriorityOfTasks = 0;
  int numberOfTasks = 0;
  mword epochLengthTicks;
  mword oneVirtualTimeUnit;
  mword virtualTimeConsumed;
  mword previousTimerInterruptTicks = 0;
  mword minvRuntime;

  // very simple N-class prio scheduling
  BasicLock readyLock;
  BasicLock printLock;

  volatile mword readyCount;
  Tree<ThreadNode> *readyTree;
  volatile mword preemption;
  volatile mword resumption;

  Scheduler* partner;

  template<typename... Args>
  inline void switchThread(Scheduler* target, Args&... a);

  inline void enqueue(Thread& t);

  Scheduler(const Scheduler&) = delete;                  // no copy
  const Scheduler& operator=(const Scheduler&) = delete; // no assignment

public:
  Scheduler();

  bool switchTest(Thread * test);
  void setPartner(Scheduler& s) { partner = &s; }
  static void resume(Thread& t);
  void preempt();
  void suspend(BasicLock& lk);
  void suspend(BasicLock& lk1, BasicLock& lk2);
  void terminate() __noreturn;
};

#endif /* _Scheduler_h_ */
