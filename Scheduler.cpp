// Greedy/Scheduler.cpp

#include "Interfaces.h"
#include "Scheduler.hpp"
#include <climits>

#define MAX_MACH 512
#define MAX_TASKS 1 << 20
#define INVALID_U UINT_MAX

// Machine lists by CPU type
static MachineId_t list_x86[MAX_MACH];
static MachineId_t list_arm[MAX_MACH];
static MachineId_t list_power[MAX_MACH];
static MachineId_t list_riscv[MAX_MACH];

static unsigned cnt_x86 = 0;
static unsigned cnt_arm = 0;
static unsigned cnt_power = 0;
static unsigned cnt_riscv = 0;

static unsigned total_machines = 0;
static bool initiated = false;

// Machine created VMs
static VMId_t vm_linux_by_machine[MAX_MACH];
static VMId_t vm_linuxrt_by_machine[MAX_MACH];
static VMId_t vm_win_by_machine[MAX_MACH];
static VMId_t vm_aix_by_machine[MAX_MACH];

static unsigned qcount_by_machine[MAX_MACH];
static unsigned task_to_midx[MAX_TASKS];

static void set_uarr (unsigned* a, unsigned n, unsigned v)
{
  unsigned i = 0;
  while (i < n)
    {
      a[i] = v;
      i++;
    }
}

static bool machine_has_gpu (MachineId_t m)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  return machine_info.gpus != 0;
}

static bool machine_can_hold_req (MachineId_t m, unsigned need_mb)
{
  MachineInfo_t machine_info = Machine_GetInfo (m);
  return need_mb <= machine_info.memory_size;
}

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

// Scan one CPU pool and pick the machine that satisfies constraints.
// Returns 1 on success (writes *out_m), 0 if pool empty, -1 if no match in this
// pass.
static int greedy_pick_in_pool (const MachineId_t* pool, unsigned count,
                                bool need_gpu, unsigned mem_need_mb,
                                MachineId_t* out_m, int use_capacity_pass)
{
  if (count == 0)
    {
      return 0;
    }
  unsigned best_q = INVALID_U;
  MachineId_t best_m = (MachineId_t) 0;
  int found = 0;

  for (unsigned i = 0; i < count; ++i)
    {
      MachineId_t m = pool[i];

      if (need_gpu && !machine_has_gpu (m))
        {
          continue;
        }
      if (use_capacity_pass && !machine_can_hold_req (m, mem_need_mb))
        {
          continue;
        }

      unsigned mid = (unsigned) m;
      if (mid >= MAX_MACH)
        {
          continue;
        }

      unsigned q = qcount_by_machine[mid];
      if (!found || q < best_q)
        {
          found = 1;
          best_q = q;
          best_m = m;
        }
    }

  if (found)
    {
      *out_m = best_m;
      return 1;
    }
  return -1;
}

// Greedy selection enforce capacity or ignore capacity
static int pick_greedy_machine (CPUType_t cpu, bool need_gpu,
                                unsigned mem_need_mb, MachineId_t* out_m)
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

  int r = greedy_pick_in_pool (pool, count, need_gpu, mem_need_mb, out_m, 1);
  if (r == 1 || r == 0)
    {
      return r;
    }

  r = greedy_pick_in_pool (pool, count, need_gpu, mem_need_mb, out_m, 0);
  if (r == 1)
    {
      return 1;
    }
  else
    {
      return 0;
    }
}

void Scheduler::Init ()
{
  if (initiated)
    {
      return;
    }

  total_machines = Machine_GetTotal ();
  if (total_machines > MAX_MACH)
    {
      total_machines = MAX_MACH;
    }

  // Reset VM caches
  unsigned i = 0;
  while (i < total_machines)
    {
      vm_linux_by_machine[i] = 0;
      vm_linuxrt_by_machine[i] = 0;
      vm_win_by_machine[i] = 0;
      vm_aix_by_machine[i] = 0;
      i++;
    }

  // Reset queue lengths and mapping
  for (i = 0; i < MAX_MACH; ++i)
    {
      qcount_by_machine[i] = 0;
    }
  for (i = 0; i < MAX_TASKS; ++i)
    {
      task_to_midx[i] = INVALID_U;
    }

  // Build CPU pools
  cnt_x86 = 0;
  cnt_arm = 0;
  cnt_power = 0;
  cnt_riscv = 0;

  i = 0;
  while (i < total_machines)
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
      i++;
    }

  initiated = true;
  SimOutput ("Scheduler::Init(): Greedy ready", 3);
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

  // Task requirements
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

  // Pick best machine by min queue
  MachineId_t m;
  int ok = pick_greedy_machine (cpu_need, gpu_need, mem_need, &m);
  if (!ok)
    {
      ThrowException ("Greedy: No compatible machine for task", task_id);
      return;
    }

  // Ensure VM, enqueue, and update local queue length + mapping
  VMId_t vm = ensure_vm (m, vm_need);
  VM_AddTask (vm, task_id, pr);

  unsigned mid = (unsigned) m;
  if (mid >= MAX_MACH)
    {
      mid = MAX_MACH - 1;
    }
  qcount_by_machine[mid] += 1;

  unsigned tid = (unsigned) task_id;
  if (tid < MAX_TASKS)
    {
      task_to_midx[tid] = mid;
    }
}

void Scheduler::PeriodicCheck (Time_t now)
{
  double e = Machine_GetClusterEnergy ();
  (void) e;
}

void Scheduler::Shutdown (Time_t time)
{

  // Shutdown only VMs we created
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
  SimOutput ("SimulationComplete(): Greedy shutdown", 3);
}

void Scheduler::TaskComplete (Time_t /*now*/, TaskId_t task_id)
{
  unsigned tid = (unsigned) task_id;
  if (tid < MAX_TASKS)
    {
      unsigned mid = task_to_midx[tid];
      if (mid != INVALID_U)
        {
          if (mid < MAX_MACH && qcount_by_machine[mid] > 0)
            {
              qcount_by_machine[mid] -= 1;
            }
          task_to_midx[tid] = INVALID_U;
        }
    }
  SimOutput ("Scheduler::TaskComplete(): Greedy compat", 4);
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