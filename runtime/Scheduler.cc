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
#include "runtime/RuntimeImpl.h"
#include "runtime/Scheduler.h"
#include "runtime/Stack.h"
#include "runtime/Thread.h"
#include "kernel/Output.h"

#include "world/Access.h"
#include "machine/Machine.h"
#include "devices/Keyboard.h"

/***********************************
    Used as a node in the tree to
	reference the thread instance
***********************************/
class ThreadNode{
	friend class Scheduler;
	Thread *th;

	public:
		bool operator < (ThreadNode other) const {
			return th->vRuntime < other.th->vRuntime;
		}
		bool operator == (ThreadNode other) const {
			return th->vRuntime == other.th->vRuntime;
		}
		bool operator > (ThreadNode other) const {
			return th->vRuntime > other.th->vRuntime;
		}
    Thread* getThread() {
      return th;
    }
	//this is how we want to do it
	ThreadNode(Thread *t){
		th = t;
	}
};

//Global variables
mword schedMinGranularityTicks = 4; //Initialized before getting set to ticks
mword defaultEpochLengthTicks = 20 ;

//Assigns the parsed parameters from Kernel.cc
extern "C" void setSchedParameters(mword mingranularity, mword epochlen) {

  //Assign parsed parameters for global variables
  schedMinGranularityTicks = mingranularity;
  defaultEpochLengthTicks = epochlen;

}


//Function to add threads in the avl tree
void add(Tree<ThreadNode> * tree, Thread * thread) {
  tree->insert(*(new ThreadNode(thread)));
}

/***********************************
			Constructor
***********************************/
 Scheduler::Scheduler() : readyCount(0), preemption(0), resumption(0), partner(this) {
	//Initialize the idle thread
	//(It keeps the CPU awake when there are no other threads currently running)
	Thread* idleThread = Thread::create((vaddr)idleStack, minimumStack);
	idleThread->setAffinity(this)->setPriority(idlePriority);
	// use low-level routines, since runtime context might not exist
	idleThread->stackPointer = stackInit(idleThread->stackPointer, &Runtime::getDefaultMemoryContext(), (ptr_t)Runtime::idleLoop, this, nullptr, nullptr);

	//Initialize the tree that contains the threads waiting to be served
	readyTree = new Tree<ThreadNode>();

	//Add the idle thread to the tree
  add(readyTree, idleThread);
	//readyTree->insert(*(new ThreadNode(idleThread)));
	readyCount += 1;
}

static inline void unlock() {}

template<typename... Args>
static inline void unlock(BasicLock &l, Args&... a) {
  l.release();
  unlock(a...);
}


/***********************************
    Gets called whenever a thread
	should be added to the tree
***********************************/
void Scheduler::enqueue(Thread& t) {
  GENASSERT1(t.priority < maxPriority, t.priority);
  readyLock.acquire();

  //Sum up the priority for every new task enqueued
  if (t.newThreadCreated == 0) {
    totalPriorityOfTasks = totalPriorityOfTasks + t.priority;
    t.newThreadCreated = 1; //Indicates true

    t.vRuntime = minvRuntime;

  }

  //Update the epoch length ticks after task is added to the tree
  bool wake = (readyCount == 0);
      add(readyTree, &t);
  readyCount += 1;
  readyLock.release();
  Runtime::debugS("Thread ", FmtHex(&t), " queued on ", FmtHex(this));
  updateEpochLength();
  if (wake) Runtime::wakeUp(this);
}

/*
Updates the epoch length in ticks every time a task is newly added to the tree
terminated, suspended or resumed back onto the tree
*/
void Scheduler::updateEpochLength() {

  if (defaultEpochLengthTicks >= (readyCount * schedMinGranularityTicks)) {
    epochLengthTicks = defaultEpochLengthTicks;
  }
  else {
    epochLengthTicks = readyCount * schedMinGranularityTicks;
  }
}

/***********************************
    Gets triggered at every RTC
	interrupt (per Scheduler)
***********************************/
void Scheduler::preempt(){		// IRQs disabled, lock count inflated

	//Get current running thread
	Thread* currentThread = Runtime::getCurrThread();

	//Get its target scheduler
	Scheduler* target = currentThread->getAffinity();

  //Make sure the epoch length in ticks is not empty
  if (!epochLengthTicks)
    updateEpochLength();

  //Check if totalPriorityOfTasks is 0, if not, calculate one virtual time unit
  if (totalPriorityOfTasks) {

    oneVirtualTimeUnit = epochLengthTicks/totalPriorityOfTasks;
  }

  //Otherwise one virtual time unit is equivalent to the epoch length
  else {
    oneVirtualTimeUnit = epochLengthTicks;
  }


  //Initially, get ticks for the first timer interrupt
  mword timerInterruptTicks = CPU::readTSC();
  mword backup = timerInterruptTicks;

  //Then get the tsc ticks b/w two consecutive timer interrupts
  timerInterruptTicks = timerInterruptTicks - previousTimerInterruptTicks;
  previousTimerInterruptTicks = backup;

  virtualTimeConsumed = (timerInterruptTicks/oneVirtualTimeUnit) * currentThread->priority;

  //Update the vruntime of the current task based on the virtual time consumed
  currentThread->vRuntime = currentThread->vRuntime + virtualTimeConsumed;

  //Make sure the ready tree is not empty before reading the leftmost node
  if (readyTree->empty())
    return;

  ThreadNode* leftmostTask = readyTree->readMinNode();

  //If the currently running task ran for mingranularity
  if (currentThread->vRuntime > schedMinGranularityTicks) {

    if (leftmostTask->getThread()->vRuntime < currentThread->vRuntime) {

      //Update minvRuntime
      minvRuntime = leftmostTask->getThread()->vRuntime;

      //schedule new task
      //Note: current task gets added back to ready tree in enqueue
      switchThread(this);


    }
  }
}

/***********************************
    Checks if it is time to stop
	serving the current thread
	and start serving the next
	one
***********************************/
bool Scheduler::switchTest(Thread* t){

	t->vRuntime++;
	if (t->vRuntime % 10 == 0)
		return true;
	return false;															//Otherwise return that the thread should not be switched
}

// very simple N-class prio scheduling!
template<typename... Args>
inline void Scheduler::switchThread(Scheduler* target, Args&... a) {
  preemption += 1;
  CHECK_LOCK_MIN(sizeof...(Args));
  Thread* nextThread;
  readyLock.acquire();
  if(!readyTree->empty()){
      nextThread = readyTree->popMinNode()->getThread();
    //  KOUT::out1("removed thread: ");
    //  KOUT::outl(FmtHex(nextThread));
      readyCount -= 1;
      goto threadFound;
  }
  readyLock.release();
  GENASSERT0(target);
  GENASSERT0(!sizeof...(Args));
  return;                                         // return to current thread

threadFound:
  readyLock.release();
  resumption += 1;
  Thread* currThread = Runtime::getCurrThread();

  GENASSERTN(currThread && nextThread && nextThread != currThread, currThread, ' ', nextThread);

  if (target) currThread->nextScheduler = target; // yield/preempt to given processor
  else currThread->nextScheduler = this;          // suspend/resume to same processor
  unlock(a...);                                   // ...thus can unlock now
  CHECK_LOCK_COUNT(1);
  Runtime::debugS("Thread switch <", (target ? 'Y' : 'S'), ">: ", FmtHex(currThread), '(', FmtHex(currThread->stackPointer), ") to ", FmtHex(nextThread), '(', FmtHex(nextThread->stackPointer), ')');

  Runtime::MemoryContext& ctx = Runtime::getMemoryContext();
  Runtime::setCurrThread(nextThread);
  Thread* prevThread = stackSwitch(currThread, target, &currThread->stackPointer, nextThread->stackPointer);
  // REMEMBER: Thread might have migrated from other processor, so 'this'
  //           might not be currThread's Scheduler object anymore.
  //           However, 'this' points to prevThread's Scheduler object.
  Runtime::postResume(false, *prevThread, ctx);
  if (currThread->state == Thread::Cancelled) {
    currThread->state = Thread::Finishing;
    switchThread(nullptr);
    unreachable();
  }
}

void Scheduler::suspend(BasicLock& lk) {
  Runtime::FakeLock fl;

  Thread* t = Runtime::getCurrThread();

  //add thread with vruntime to readytree and update vruntime
  readyTree->deleteNode(*(new ThreadNode(t)));
  t->vRuntime = t->vRuntime - minvRuntime;


  updateEpochLength();
  switchThread(nullptr, lk);

}

void Scheduler::suspend(BasicLock& lk1, BasicLock& lk2) {
  Runtime::FakeLock fl;

  Thread* t = Runtime::getCurrThread();
  readyTree->deleteNode(*(new ThreadNode(t)));
  t->vRuntime = t->vRuntime - minvRuntime;


  updateEpochLength();
  switchThread(nullptr, lk1, lk2);

}

void Scheduler::resume(Thread& t) {
  GENASSERT1(&t != Runtime::getCurrThread(), Runtime::getCurrThread());
  Scheduler * scheduler = (t.nextScheduler ? t.nextScheduler : Runtime::getScheduler());

  //Update runtime of the current thread
  t.vRuntime = t.vRuntime + scheduler->minvRuntime;

  //Resume thread
  scheduler->enqueue(t);
}

void Scheduler::terminate() {
  Runtime::RealLock rl;
  Thread* thr = Runtime::getCurrThread();
  GENASSERT1(thr->state != Thread::Blocked, thr->state);
  thr->state = Thread::Finishing;
  switchThread(nullptr);
  unreachable();
}

extern "C" Thread* postSwitch(Thread* prevThread, Scheduler* target) {
  CHECK_LOCK_COUNT(1);
  if fastpath(target) Scheduler::resume(*prevThread);
  return prevThread;
}

extern "C" void invokeThread(Thread* prevThread, Runtime::MemoryContext* ctx, funcvoid3_t func, ptr_t arg1, ptr_t arg2, ptr_t arg3) {
  Runtime::postResume(true, *prevThread, *ctx);
  func(arg1, arg2, arg3);
  Runtime::getScheduler()->terminate();
}
