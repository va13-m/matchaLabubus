// RoundRobin/Scheduler.cpp

#include "Scheduler.hpp"

// Limits and globals
#define MAX_NUM_MACHINES 512

static bool migrating = false;
static unsigned active_machines = 16;

static unsigned total_machines = 0;
static bool initiated = false;

// Count number of machines in each CPU pool
static unsigned ct_86 = 0;
static unsigned ct_arm = 0;
static unsigned ct_power = 0;
static unsigned ct_riscv = 0;

// Round robin indices left per CPU pool
static unsigned rr_86 = 0;
static unsigned rr_arm = 0;
static unsigned rr_power = 0;
static unsigned rr_riscv = 0;

// Machine created VM IDs
static VMId_t vm_linux_by_machine[MAX_NUM_MACHINES];
static VMId_t vm_linux_rt_by_machine[MAX_NUM_MACHINES];
static VMId_t vm_win_by_machine[MAX_NUM_MACHINES];
static VMId_t vm_aix_by_machine[MAX_NUM_MACHINES];

// Hold machine IDs by architecture
static MachineId_t list_86[MAX_NUM_MACHINES];
static MachineId_t list_arm[MAX_NUM_MACHINES];
static MachineId_t list_power[MAX_NUM_MACHINES];
static MachineId_t list_riscv[MAX_NUM_MACHINES];

// Helpers
static bool machine_gpu (MachineId_t machine);
static bool m_req (MachineId_t m, unsigned mem_mb);
static VMId_t vm_ensure (MachineId_t machine, VMType_t wanted);
static int round_robin_pool (MachineId_t* list, unsigned* rr_idx,
                             unsigned count, bool gpu_required,
                             unsigned mem_need_mb, MachineId_t* out_m,
                             int use_capacity_pass);
static int round_robin_m (CPUType_t req_cpu, bool GPU_cap, unsigned needed_mem,
                          MachineId_t* machine);

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

  // Clamp to arrays
  total_machines = Machine_GetTotal ();
  if (total_machines > MAX_NUM_MACHINES)
    {
      total_machines = MAX_NUM_MACHINES;
    }

  // Reset VM caches
  for (unsigned i = 0; i < total_machines; ++i)
    {
      vm_linux_by_machine[i] = 0;
      vm_linux_rt_by_machine[i] = 0;
      vm_win_by_machine[i] = 0;
      vm_aix_by_machine[i] = 0;
    }

  // Rebuild CPU pools
  ct_86 = ct_arm = ct_power = ct_riscv = 0;
  for (unsigned i = 0; i < total_machines; ++i)
    {
      CPUType_t cpu = Machine_GetCPUType ((MachineId_t) i);
      if (cpu == X86)
        {
          list_86[ct_86++] = (MachineId_t) (i);
        }
      else if (cpu == ARM)
        {
          list_arm[ct_arm++] = (MachineId_t) (i);
        }
      else if (cpu == POWER)
        {
          list_power[ct_power++] = (MachineId_t) (i);
        }
      else
        {
          list_riscv[ct_riscv++] = (MachineId_t) (i);
        }
    }

  // Reset RR index for all pools
  rr_86 = rr_arm = rr_power = rr_riscv = 0;

  initiated = true;

  SimOutput ("Scheduler::Init() ready", 3);
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

  // Gather task requirements from the simulator
  VMType_t req_vm = RequiredVMType (task_id);
  CPUType_t req_cpu = RequiredCPUType (task_id); // X86,ARM,POWER,RISCV
  bool GPU_cap = IsTaskGPUCapable (task_id);     // Needed GPU
  unsigned needed_mem = GetTaskMemory (task_id);

  // Map SLA to Priority
  Priority_t pr = LOW_PRIORITY;
  SLAType_t req_sla = RequiredSLA (task_id);
  if (req_sla == SLA0)
    {
      pr = HIGH_PRIORITY;
    }
  else if (req_sla == SLA1)
    {
      pr = MID_PRIORITY;
    }

  // Pick a compatible machine
  MachineId_t machine;
  int compatible = round_robin_m (req_cpu, GPU_cap, needed_mem, &machine);
  if (!compatible)
    {
      // No compatible pool
      ThrowException ("No compatible machine for task", task_id);
      return;
    }

  // Ensure we have the right VM type on that machine
  VMId_t vm = vm_ensure (machine, req_vm);
  VM_AddTask (vm, task_id, pr);
}

void Scheduler::PeriodicCheck (Time_t now)
{
  double e = Machine_GetClusterEnergy ();
  (void) e;
}

void Scheduler::Shutdown (Time_t time)
{

  // Shutdown any VMs we created
  for (unsigned i = 0; i < total_machines; ++i)
    {
      if (vm_linux_by_machine[i] != 0)
        {
          VM_Shutdown (vm_linux_by_machine[i]);
        }
      if (vm_linux_rt_by_machine[i] != 0)
        {
          VM_Shutdown (vm_linux_rt_by_machine[i]);
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
  SimOutput ("SimulationComplete(): Time is " + to_string (time), 4);
}

void Scheduler::TaskComplete (Time_t now, TaskId_t task_id)
{
  // Do any bookkeeping necessary for the data structures
  // Decide if a machine is to be turned off, slowed down, or VMs to be migrated
  // according to your policy This is an opportunity to make any adjustments to
  // optimize performance/energy
  SimOutput ("Scheduler::TaskComplete(): Task " + to_string (task_id) +
                 " is complete at " + to_string (now),
             4);
}

// Helpers

// True if machine has any GPUs
static bool machine_gpu (MachineId_t machine)
{
  MachineInfo_t info = Machine_GetInfo (machine);
  return info.gpus != 0;
}

// Check against total capacity
static bool m_req (MachineId_t m, unsigned mem_mb)
{
  MachineInfo_t info = Machine_GetInfo (m);
  return mem_mb <= info.memory_size;
}

// Ensure a VM of the requested type exists on the selected machine, then
// return its id
static VMId_t vm_ensure (MachineId_t machine, VMType_t wanted)
{
  unsigned mid = (unsigned) machine;
  if (mid >= MAX_NUM_MACHINES)
    {
      mid = MAX_NUM_MACHINES - 1;
    }
  CPUType_t cpu = Machine_GetCPUType (machine); // VM must match machine CPU

  if (wanted == LINUX)
    {
      VMId_t vm = vm_linux_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (LINUX, cpu);
          VM_Attach (vm, machine);
          vm_linux_by_machine[mid] = vm;
        }
      return vm;
    }
  else if (wanted == LINUX_RT)
    {
      VMId_t vm = vm_linux_rt_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (LINUX_RT, cpu);
          VM_Attach (vm, machine);
          vm_linux_rt_by_machine[mid] = vm;
        }
      return vm;
    }
  else if (wanted == WIN)
    {
      VMId_t vm = vm_win_by_machine[mid];
      if (vm == 0)
        {
          vm = VM_Create (WIN, cpu);
          VM_Attach (vm, machine);
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
          VM_Attach (vm, machine);
          vm_aix_by_machine[mid] = vm;
        }
      return vm;
    }
}

// Pick within a CPU pool. Return 1 if success, 0 if empty pool, -1 if pool
// exists but no match in this pass.
static int round_robin_pool (MachineId_t* list, unsigned* rr_idx,
                             unsigned count, bool gpu_required,
                             unsigned mem_need_mb, MachineId_t* out_m,
                             int use_capacity_pass)
{
  if (count == 0)
    {
      return 0; // no machines of this CPU type
    }

  unsigned start = *rr_idx; // remember where we left off
  for (unsigned i = 0; i < count; ++i)
    {
      // Walk of the pool
      unsigned idx = start + i;
      if (idx >= count)
        {
          idx -= count;
        }

      MachineId_t m = list[idx];

      // GPU filter
      if (gpu_required && !machine_gpu (m))
        {
          continue;
        }

      // Enforce relaxed capacity
      if (use_capacity_pass && !m_req (m, mem_need_mb))
        {
          continue;
        }

      // Success return this machine and advance RR index
      *out_m = m;
      *rr_idx = idx + 1;
      if (*rr_idx >= count)
        {
          *rr_idx = 0;
        }
      return 1;
    }
  return -1; // nothing matched this pass
}

// CPU Awareness
static int round_robin_m (CPUType_t req_cpu, bool GPU_cap, unsigned needed_mem,
                          MachineId_t* machine)
{
  int r = 0;

  if (req_cpu == X86)
    {
      r = round_robin_pool (list_86, &rr_86, ct_86, GPU_cap, needed_mem,
                            machine, 1);
      if (r == 1 || r == 0)
        {
          return r;
        }

      r = round_robin_pool (list_86, &rr_86, ct_86, GPU_cap, needed_mem,
                            machine, 0);
      if (r == 1)
        {
          return 1;
        }
      else
        {
          return 0;
        }
    }
  else if (req_cpu == ARM)
    {
      r = round_robin_pool (list_arm, &rr_arm, ct_arm, GPU_cap, needed_mem,
                            machine, 1);
      if (r == 1 || r == 0)
        {
          return r;
        }
      r = round_robin_pool (list_arm, &rr_arm, ct_arm, GPU_cap, needed_mem,
                            machine, 0);
      if (r == 1)
        {
          return 1;
        }
      else
        {
          return 0;
        }
    }
  else if (req_cpu == POWER)
    {
      r = round_robin_pool (list_power, &rr_power, ct_power, GPU_cap,
                            needed_mem, machine, 1);
      if (r == 1 || r == 0)
        {
          return r;
        }

      r = round_robin_pool (list_power, &rr_power, ct_power, GPU_cap,
                            needed_mem, machine, 0);
      if (r == 1)
        {
          return 1;
        }
      else
        {
          return 0;
        }
    }
  else
    {
      r = round_robin_pool (list_riscv, &rr_riscv, ct_riscv, GPU_cap,
                            needed_mem, machine, 1);
      if (r == 1 || r == 0)
        {
          return r;
        }
      r = round_robin_pool (list_riscv, &rr_riscv, ct_riscv, GPU_cap,
                            needed_mem, machine, 0);
      if (r == 1)
        {
          return 1;
        }
      else
        {
          return 0;
        }
    }
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

void SLAWarning (Time_t time, TaskId_t task_id)
{
  if (!IsTaskCompleted (task_id))
    {
      SetTaskPriority (task_id, HIGH_PRIORITY);
    }
}

void StateChangeComplete (Time_t time, MachineId_t machine_id)
{
  // Called in response to an earlier request to change the state of a machine
}
