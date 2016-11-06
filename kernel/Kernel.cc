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
#include "runtime/Thread.h"
#include "kernel/AddressSpace.h"
#include "kernel/Clock.h"
#include "kernel/Output.h"
#include "world/Access.h"
#include "machine/Machine.h"
#include "devices/Keyboard.h"

#include "main/UserMain.h"
#include "runtime/Scheduler.h"
#include "generic/tree.h"

#include <string>
#include <iostream>
#include <cctype>


using namespace std;

AddressSpace kernelSpace(true); // AddressSpace.h
volatile mword Clock::tick;     // Clock.h

extern Keyboard keyboard;

#if TESTING_KEYCODE_LOOP
static void keybLoop() {
  for (;;) {
    Keyboard::KeyCode c = keyboard.read();
    StdErr.print(' ', FmtHex(c));
  }
}
#endif

void kosMain() {
  KOUT::outl("Welcome to KOS!", kendl);
  auto iter = kernelFS.find("motb");

  //Searching and assigning schedparam file to iter2
  auto schedParam = kernelFS.find("schedparam");

  if (iter == kernelFS.end()) {
    KOUT::outl("motb information not found");
  } else {
    FileAccess f(iter->second);
    for (;;) {
      char c;
      if (f.read(&c, 1) == 0) break;
      KOUT::out1(c);
    }
    KOUT::outl();
  }

  //Get number of cycles per sec
  mword a = CPU::readTSC();
  Timeout::sleep(Clock::now() + 1000);
  mword b = CPU::readTSC();
  mword ticksPerSec = b-a;

  //Calculate ticks per millisec
  mword ticksPerMsec = ticksPerSec/1000;

  //Accessing schedParam file and printing the contents to
  //boot screen given the file exists and is not empty
  if (schedParam == kernelFS.end()) {
    KOUT::outl("schedparam file not found");
  }

  else {

    //Array holds contents of schedParam
    char schedParamArray[100];
    int counter = 0;

    FileAccess f(schedParam->second);
    for (;;) {
      char c;
      if (f.read(&c, 1) == 0) break;

      //Get character input from schedParam into array
      schedParamArray[counter] = c;
      counter++;
    }

    //Adds null to indicated end of array
    schedParamArray[counter] = '\0';

    int n = 0;
    int minGranularity = 0;
    int epochLen = 0;
    int intStorage = 0;

    //initially set to false
    int trueInt = 0;

    //Mike did this O(n^3) beauty
    //Cycle through the array until the parameter numbers are
    //obtained
    while (schedParamArray[n] != '\0') {

      //Check if the char in the array is a digit and
      //if the first parameter hasn't been already obtained
      if (isdigit(schedParamArray[n]) && trueInt == 0) {

        //Loop through until the first full integer has been obtained
        while (isdigit(schedParamArray[n])) {

          intStorage = schedParamArray[n]; //Obtain the ascii value
          intStorage = intStorage - 48;    //Converts ascii value to int

          minGranularity = minGranularity * 10; //Combines multiple int to single int
          minGranularity = minGranularity + intStorage;

          n++;
        }
        trueInt = 1; //Set to true -> first param obtained
        intStorage = 0; //Reset value to 0
      }

      while (isdigit(schedParamArray[n])) {

        intStorage = schedParamArray[n] - 48;

        epochLen = epochLen * 10;
        epochLen = epochLen + intStorage;

        n++;
      }
      n++;
    }

    //Convert millisec values to ticks
    mword minGranularityTicks = minGranularity * ticksPerMsec;
    mword epochLenTicks = epochLen * ticksPerMsec;

    setSchedParameters(minGranularityTicks, epochLenTicks);

    //Printing parameters to boot screen
    for (int i = 0; i < strlen(schedParamArray); i++) {

      KOUT::out1(schedParamArray[i]);

    }

    KOUT::outl("Recalibrated parameters");
    KOUT::out1("minGranularityTicks: ");
    KOUT::outl(minGranularityTicks);
    KOUT::out1("epochLenTicks: ");
    KOUT::outl(epochLenTicks);
    KOUT::outl();



  }

#if TESTING_AVL_TREE
  Tree<int> tree;

  //Inserts some nodes into the tree
  tree.insert(5);
  tree.insert(2);
  tree.insert(3);

  /*---Test Case 1---*/
  //Tests if minimum node was popped
  
  int expectedValue = 2;
  int actualPopValue = *(tree.popMinNode());

  if (expectedValue == actualPopValue) {
    KOUT::outl("Test Case 1 Passed");
    KOUT::outl();
  }
  else {
    KOUT::outl("Test Case 1 Failed");
    KOUT::outl();
  }

  /*---Test Case 2---*/
  //Find 3 in the tree, if null or not there
  //test fails

  expectedValue = 3;
  Tree<int>::node* actualFindValue = tree.find(3);

  if (actualFindValue && (expectedValue == actualFindValue -> item)){
    KOUT::outl("Test Case 2 Passed");
    KOUT::outl();
  }
  else {
    KOUT::outl("Test Case 2 Failed");
    KOUT::outl();
  }

  /*---Test Case 3---*/
  //Tries to find 4 (which isn't in the tree)
  //Should return null

  actualFindValue = tree.find(4);

  if (!actualFindValue){
    KOUT::outl("Test Case 3 Passed");
    KOUT::outl();
  }
  else {
    KOUT::outl("Test Case 3 Failed");
    KOUT::outl();
  }

  /*---Test Case 4---*/
  //Deletes node containing 3 and checks if its
  //still there

  tree.deleteNode(3);

  actualFindValue = tree.find(3);

  if (!actualFindValue){
    KOUT::outl("Test Case 4 Passed");
    KOUT::outl();
  }
  else {
    KOUT::outl("Test Case 4 Failed");
    KOUT::outl();
  }

  /*---Test Case 5---*/
  //Tests if read min node returns the min node
  //without removing it

  if (tree.readMinNode() == tree.popMinNode()) {
    KOUT::outl("Test Case 5 Passed");
    KOUT::outl();
  }
  else {
    KOUT::outl("Test Case 5 Failed");
    KOUT::outl();
  }


#endif

#if TESTING_TIMER_TEST
  StdErr.print(" timer test, 3 secs...");
  for (int i = 0; i < 3; i++) {
    Timeout::sleep(Clock::now() + 1000);
    StdErr.print(' ', i+1);
  }
  StdErr.print(" done.", kendl);
#endif
#if TESTING_KEYCODE_LOOP
  Thread* t = Thread::create()->setPriority(topPriority);
  Machine::setAffinity(*t, 0);
  t->start((ptr_t)keybLoop);
#endif
  Thread::create()->start((ptr_t)UserMain);
#if TESTING_PING_LOOP
  for (;;) {
    Timeout::sleep(Clock::now() + 1000);
    KOUT::outl("...ping...");
  }
#endif
}

extern "C" void kmain(mword magic, mword addr, mword idx)         __section(".boot.text");
extern "C" void kmain(mword magic, mword addr, mword idx) {
  if (magic == 0 && addr == 0xE85250D6) {
    // low-level machine-dependent initialization on AP
    Machine::initAP(idx);
  } else {
    // low-level machine-dependent initialization on BSP -> starts kosMain
    Machine::initBSP(magic, addr, idx);
  }
}
