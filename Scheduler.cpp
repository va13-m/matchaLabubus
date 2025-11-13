// MinMin/Scheduler.cpp

#include "Scheduler.hpp"
#include <climits>

#define MAX_MACH 512
#define MAX_TASKS 1 << 20
#define INVALID_U UINT_MAX

// CPU machine pools
static MachineId_t list_x86[MAX_MACH];
static MachineId_t list_arm[MAX_MACH];
static MachineId_t list_power[MAX_MACH];
static MachineId_t list_riscv[MAX_MACH];
static unsigned cnt_x86 = 0, cnt_arm = 0, cnt_power = 0, cnt_riscv = 0;

// Created VM ids per machine
static VMId_t vm_linux_by_machine[MAX_MACH];
static VMId_t vm_linuxrt_by_machine[MAX_MACH];
static VMId_t vm_win_by_machine[MAX_MACH];
static VMId_t vm_aix_by_machine[MAX_MACH];

// Queue counts and a task→machine map
static unsigned qcount_by_machine[MAX_MACH]; // local queued tasks
static unsigned task_to_midx[MAX_TASKS];

// Cluster bookkeeping
static unsigned total_machines = 0;
static bool initiated = false;

// helpers

// True if machine has any GPU devices
static bool machine_has_gpu (MachineId_t m)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  return machine_info.gpus != 0;
}

// Check against total capacity
static bool machine_can_hold_req (MachineId_t m, unsigned need_mb)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  return need_mb <= machine_info.memory_size;
}

// Return a VM of the desired type on machine m
static VMId_t ensure_vm (MachineId_t m, VMType_t want)
{
  unsigned mid = (unsigned) m;
  if (mid >= MAX_MACH)
    {
      mid = MAX_MACH - 1;
    }

  CPUType_t cpu = Machine_GetCPUType (m);
  if (want == LINUX)
    {
      VMId_t vm = vm_linux_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (LINUX, cpu);
          VM_Attach (vm, m);
          vm_linux_by_machine[mid] = vm;
        }
      return vm;
    }
  else if (want == LINUX_RT)
    {
      VMId_t vm = vm_linuxrt_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (LINUX_RT, cpu);
          VM_Attach (vm, m);
          vm_linuxrt_by_machine[mid] = vm;
        }
      return vm;
    }
  else if (want == WIN)
    {
      VMId_t vm = vm_win_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (WIN, cpu);
          VM_Attach (vm, m);
          vm_win_by_machine[mid] = vm;
        }
      return vm;
    }
  else
    {
      VMId_t vm = vm_aix_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (AIX, cpu);
          VM_Attach (vm, m);
          vm_aix_by_machine[mid] = vm;
        }
      return vm;
    }
}

// Estimate solo runtime on a machine using its peak perf entry
static double solo_time_on (MachineId_t m, unsigned long long total_instr)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  unsigned peak = 1;
  if (machine_info.performance.size () > 0)
    {
      peak = machine_info.performance[0];
      if (peak == 0)
        {
          peak = 1;
        }
    }
  return (double) total_instr / (double) peak;
}

// Min–Min passes:
// Pass 1: respect CPU/GPU and capacity, pick machine minimizing
// Pass 2: ignore capacity, still respect CPU/GPU.
// Returns 1 on success, 0 if no machine can be selected.
static int pick_minmin_machine (CPUType_t cpu, bool need_gpu,
                                unsigned mem_need_mb,
                                unsigned long long total_instr,
                                MachineId_t* out_m)
{
  const MachineId_t* pool;
  unsigned count = 0;

  if (cpu == X86)
    {
      pool = list_x86;
      count = cnt_x86;
    }
  else if (cpu == ARM)
    {
      pool = list_arm;
      count = cnt_arm;
    }
  else if (cpu == POWER)
    {
      pool = list_power;
      count = cnt_power;
    }
  else
    {
      pool = list_riscv;
      count = cnt_riscv;
    }

  if (count == 0)
    {
      return 0;
    }

  // pass 1
  {
    double best = 0.0;
    int have = 0;
    MachineId_t best_m = (MachineId_t) 0;

    for (unsigned i = 0; i < count; ++i)
      {
        MachineId_t m = pool[i];
        if (need_gpu && !machine_has_gpu (m))
          {
            continue;
          }
        if (!machine_can_hold_req (m, mem_need_mb))
          {
            continue;
          }
        double solo = solo_time_on (m, total_instr);
        double est = (double) qcount_by_machine[(unsigned) m] * solo +
                     solo; // wait and run
        if (!have || est < best)
          {
            have = 1;
            best = est;
            best_m = m;
          }
      }
    if (have)
      {
        *out_m = best_m;
        return 1;
      }
  }

  // pass 2
  {
    double best = 0.0;
    int have = 0;
    MachineId_t best_m = (MachineId_t) 0;

    for (unsigned i = 0; i < count; ++i)
      {
        MachineId_t m = pool[i];
        if (need_gpu && !machine_has_gpu (m))
          {
            continue;
          }
        double solo = solo_time_on (m, total_instr);
        double est = (double) qcount_by_machine[(unsigned) m] * solo + solo;
        if (!have || est < best)
          {
            have = 1;
            best = est;
            best_m = m;
          }
      }
    if (have)
      {
        *out_m = best_m;
        return 1;
      }
  }

  return 0;
}

void Scheduler::Init ()
{
  SimOutput ("Scheduler::Init(): Total number of machines is " +
                 to_string (Machine_GetTotal ()),
             3);
  SimOutput ("Scheduler::Init(): Initializing scheduler", 1);

  if (initiated)
    {
      return;
    }
  total_machines = Machine_GetTotal ();
  if (total_machines > MAX_MACH)
    {
      total_machines = MAX_MACH;
    }

  // Reset per-machine VM caches
  for (unsigned i = 0; i < total_machines; ++i)
    {
      vm_linux_by_machine[i] = 0;
      vm_linuxrt_by_machine[i] = 0;
      vm_win_by_machine[i] = 0;
      vm_aix_by_machine[i] = 0;
    }

  // Reset queue counts and task map
  for (unsigned i = 0; i < MAX_MACH; ++i)
    {
      qcount_by_machine[i] = 0;
    }
  for (unsigned i = 0; i < MAX_TASKS; ++i)
    {
      task_to_midx[i] = INVALID_U;
    }

  // Build CPU pools
  cnt_x86 = 0;
  cnt_arm = 0;
  cnt_power = 0;
  cnt_riscv = 0;
  for (unsigned i = 0; i < total_machines; ++i)
    {
      CPUType_t ct = Machine_GetCPUType (MachineId_t (i));
      if (ct == X86)
        {
          list_x86[cnt_x86++] = MachineId_t (i);
        }
      else if (ct == ARM)
        {
          list_arm[cnt_arm++] = MachineId_t (i);
        }
      else if (ct == POWER)
        {
          list_power[cnt_power++] = MachineId_t (i);
        }
      else
        {
          list_riscv[cnt_riscv++] = MachineId_t (i);
        }
    }

  initiated = true;
  SimOutput ("Scheduler::Init(): Min–Min ready", 3);
}

void Scheduler::MigrationComplete (Time_t time, VMId_t vm_id)
{
  // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask (Time_t now, TaskId_t task_id)
{

  if (!initiated)
    {
      Init ();
    }
  // Pull task requirements
  TaskInfo_t task_info = GetTaskInfo (task_id);
  VMType_t vm_need = task_info.required_vm;
  CPUType_t cpu_need = task_info.required_cpu;
  bool gpu_need = task_info.gpu_capable; // if true, treat as requiring GPU pool
  unsigned mem_need = task_info.required_memory;
  unsigned long long instr = task_info.total_instructions;

  // SLA → Priority
  Priority_t pr = LOW_PRIORITY;
  if (task_info.required_sla == SLA0)
    {
      pr = HIGH_PRIORITY;
    }
  else if (task_info.required_sla == SLA1)
    {
      pr = MID_PRIORITY;
    }

  // Pick compatible machine minimizing est. finish time on that machine
  MachineId_t m;
  int ok = pick_minmin_machine (cpu_need, gpu_need, mem_need, instr, &m);
  if (!ok)
    {
      // incompatibility
      ThrowException ("MinMin: No compatible machine for task", task_id);
      return;
    }

  // Ensure VM exists on that machine, then enqueue task
  VMId_t vm = ensure_vm (m, vm_need);
  VM_AddTask (vm, task_id, pr);

  // Update local queue count + placement map
  qcount_by_machine[(unsigned) m] = qcount_by_machine[(unsigned) m] + 1;
  unsigned tid = (unsigned) task_id;
  if (tid < MAX_TASKS)
    {
      task_to_midx[tid] = (unsigned) m;
    }
}

void Scheduler::PeriodicCheck (Time_t now)
{

  double e = Machine_GetClusterEnergy ();
  (void) e;
}

void Scheduler::Shutdown (Time_t time)
{

  // Shut down only VMs we lazily created
  for (unsigned i = 0; i < total_machines; ++i)
    {
      if (vm_linux_by_machine[i] != 0)
        {
          VM_Shutdown (vm_linux_by_machine[i]);
        }
      if (vm_linuxrt_by_machine[i] != 0)
        {
          VM_Shutdown (vm_linuxrt_by_machine[i]);
        }
      if (vm_win_by_machine[i] != 0)
        {
          VM_Shutdown (vm_win_by_machine[i]);
        }
      if (vm_aix_by_machine[i] != 0)
        {
          VM_Shutdown (vm_aix_by_machine[i]);
        }
    }
  SimOutput ("SimulationComplete(): Finished!", 4);
}

void Scheduler::TaskComplete (Time_t now, TaskId_t task_id)
{

  // Decrement the local queue count where we placed this task
  unsigned tid = (unsigned) task_id;
  if (tid < MAX_TASKS)
    {
      unsigned midx = task_to_midx[tid];
      if (midx != INVALID_U)
        {
          if (qcount_by_machine[midx] > 0)
            {
              qcount_by_machine[midx] -= 1;
            }
          task_to_midx[tid] = INVALID_U;
        }
    }
  SimOutput ("Scheduler::TaskComplete()", 4);
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler ()
{
  SimOutput ("InitScheduler(): Initializing scheduler", 4);
  Scheduler.Init ();
}

void HandleNewTask (Time_t time, TaskId_t task_id)
{
  SimOutput ("HandleNewTask(): Received new task " + to_string (task_id) +
                 " at time " + to_string (time),
             4);
  Scheduler.NewTask (time, task_id);
}

void HandleTaskCompletion (Time_t time, TaskId_t task_id)
{
  SimOutput ("HandleTaskCompletion(): Task " + to_string (task_id) +
                 " completed at time " + to_string (time),
             4);
  Scheduler.TaskComplete (time, task_id);
}
void MemoryWarning (Time_t time, MachineId_t machine_id)
{
  // The simulator is alerting you that machine identified by machine_id is
  // overcommitted
  SimOutput ("MemoryWarning(): Overflow at " + to_string (machine_id) +
                 " was detected at time " + to_string (time),
             0);
}

void MigrationDone (Time_t time, VMId_t vm_id)
{
  // The function is called on to alert you that migration is complete
  SimOutput ("MigrationDone(): Migration of VM " + to_string (vm_id) +
                 " was completed at time " + to_string (time),
             4);
  Scheduler.MigrationComplete (time, vm_id);
}

void SchedulerCheck (Time_t time)
{
  // This function is called periodically by the simulator, no specific event
  SimOutput ("SchedulerCheck(): SchedulerCheck() called at " + to_string (time),
             4);
  Scheduler.PeriodicCheck (time);
}

void SimulationComplete (Time_t time)
{
  // This function is called before the simulation terminates Add whatever you
  // feel like.
  cout << "SLA violation report" << endl;
  cout << "SLA0: " << GetSLAReport (SLA0) << "%" << endl;
  cout << "SLA1: " << GetSLAReport (SLA1) << "%" << endl;
  cout << "SLA2: " << GetSLAReport (SLA2) << "%"
       << endl; // SLA3 do not have SLA violation issues
  cout << "Total Energy " << Machine_GetClusterEnergy () << "KW-Hour" << endl;
  cout << "Simulation run finished in " << double (time) / 1000000 << " seconds"
       << endl;
  SimOutput ("SimulationComplete(): Simulation finished at time " +
                 to_string (time),
             4);

  Scheduler.Shutdown (time);
}

void SLAWarning (Time_t time, TaskId_t task_id) {}

void StateChangeComplete (Time_t time, MachineId_t machine_id)
{
  // Called in response to an earlier request to change the state of a machine
}
