// LoadBalancing/Scheduler.cpp

#include "Scheduler.hpp"
#include <climits>

#define MAX_MACH 512
#define MAX_TASKS 1 << 20
#define INVALID_U UINT_MAX

static bool migrating = false;
static unsigned active_machines = 16;
static unsigned total_machines = 0;
static bool initiated = false;

// Machine lists grouped by CPU type
static MachineId_t list_x86[MAX_MACH];
static MachineId_t list_arm[MAX_MACH];
static MachineId_t list_power[MAX_MACH];
static MachineId_t list_riscv[MAX_MACH];

static unsigned cnt_x86 = 0;
static unsigned cnt_arm = 0;
static unsigned cnt_power = 0;
static unsigned cnt_riscv = 0;

// Created VM ids per machine
static VMId_t vm_linux_by_machine[MAX_MACH];
static VMId_t vm_linuxrt_by_machine[MAX_MACH];
static VMId_t vm_win_by_machine[MAX_MACH];
static VMId_t vm_aix_by_machine[MAX_MACH];

static unsigned task_to_midx[MAX_TASKS];

// helpers
static bool machine_has_gpu (MachineId_t m)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  return machine_info.gpus != 0;
}

// Check total capacity, ignore current mem
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

// CPU-aware picker:
// Pass 1 enforces capacity and GPU if needed
// Pass 2 ignores capacity (still respects CPU/GPU).
// Returns 1 if found, 0 if the CPU pool is empty or no match at all.
static int pick_lb_machine (CPUType_t cpu, bool need_gpu, unsigned mem_need_mb,
                            MachineId_t* out_m)
{
  const MachineId_t* pool = nullptr;
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
    unsigned best_load = INVALID_U;
    MachineId_t best_m = (MachineId_t) 0;
    int found = 0;

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
        MachineInfo_t mi = Machine_GetInfo (m);
        if (!found || mi.active_tasks < best_load)
          {
            found = 1;
            best_load = mi.active_tasks;
            best_m = m;
          }
      }
    if (found)
      {
        *out_m = best_m;
        return 1;
      }
  }

  // pass 2
  {
    unsigned best_load = INVALID_U;
    MachineId_t best_m = (MachineId_t) 0;
    int found = 0;

    for (unsigned i = 0; i < count; ++i)
      {
        MachineId_t m = pool[i];
        if (need_gpu && !machine_has_gpu (m))
          {
            continue;
          }

        MachineInfo_t mi = Machine_GetInfo (m);
        if (!found || mi.active_tasks < best_load)
          {
            found = 1;
            best_load = mi.active_tasks;
            best_m = m;
          }
      }
    if (found)
      {
        *out_m = best_m;
        return 1;
      }
  }

  return 0;
}

void Scheduler::Init ()
{
  if (initiated)
    {
      return;
    }

  // Clamp to static arrays
  total_machines = Machine_GetTotal ();
  if (total_machines > MAX_MACH)
    {
      total_machines = MAX_MACH;
    }

  // Reset VM caches and task map
  for (unsigned i = 0; i < total_machines; ++i)
    {
      vm_linux_by_machine[i] = 0;
      vm_linuxrt_by_machine[i] = 0;
      vm_win_by_machine[i] = 0;
      vm_aix_by_machine[i] = 0;
    }
  for (unsigned i = 0; i < MAX_TASKS; ++i)
    {
      task_to_midx[i] = INVALID_U;
    }

  // Build CPU pools (split machine ids by architecture)
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
  SimOutput ("Scheduler::Init(): LoadBal ready", 3);
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

  // Gather task requirements
  VMType_t vm_need = RequiredVMType (task_id);
  CPUType_t cpu_need = RequiredCPUType (task_id);
  bool gpu_need = IsTaskGPUCapable (task_id);
  unsigned mem_need = GetTaskMemory (task_id);

  // SLA map to Priority
  Priority_t pr = LOW_PRIORITY;
  SLAType_t sla = RequiredSLA (task_id);
  if (sla == SLA0)
    {
      pr = HIGH_PRIORITY;
    }
  else if (sla == SLA1)
    {
      pr = MID_PRIORITY;
    }

  // Choose least loaded compatible machine
  MachineId_t m;
  int ok = pick_lb_machine (cpu_need, gpu_need, mem_need, &m);
  if (!ok)
    {
      ThrowException ("LoadBal: No compatible machine for task", task_id);
      return;
    }

  // Ensure a VM of the right type exists on that machine, then enqueue
  VMId_t vm = ensure_vm (m, vm_need);
  VM_AddTask (vm, task_id, pr);

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

  // Shut down only VMs created lazily
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
  SimOutput ("SimulationComplete(): LoadBal shutdown", 3);
}

void Scheduler::TaskComplete (Time_t now, TaskId_t task_id)
{
  unsigned tid = (unsigned) task_id;
  if (tid < MAX_TASKS)
    task_to_midx[tid] = INVALID_U;
  SimOutput ("Scheduler::TaskComplete(): LoadBal compat", 4);
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
  migrating = false;
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