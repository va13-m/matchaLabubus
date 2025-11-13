machine class:
{
    Number of machines: 8
    CPU type: X86
    Number of cores: 2
    Memory: 8192
    S-States: [120, 100, 100, 80, 40, 10, 0]
    P-States: [12, 8, 6, 4]
    C-States: [12, 3, 1, 0]
    MIPS: [400, 300, 200, 100]
    GPUs: no
}




machine class:
{
    Number of machines: 8
    CPU type: X86
    Number of cores: 16
    Memory: 32768
    S-States: [120, 100, 100, 80, 40, 10, 0]
    P-States: [12, 8, 6, 4]
    C-States: [12, 3, 1, 0]
    MIPS: [2000, 1500, 1200, 800]
    GPUs: no
}




machine class:
{
    Number of machines: 8
    CPU type: X86
    Number of cores: 8
    Memory: 16384
    S-States: [120, 100, 100, 80, 40, 10, 0]
    P-States: [12, 8, 6, 4]
    C-States: [12, 3, 1, 0]
    MIPS: [1000, 800, 600, 400]
    GPUs: no
}




task class:
{
    Start time: 60000
    End time : 280000
    Inter arrival: 2200
    Expected runtime: 24000000
    Memory: 384
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA0
    CPU type: X86
    Task type: WEB
    Seed: 540001
}




task class:
{
    Start time: 120000
    End time : 180000
    Inter arrival: 1500
    Expected runtime: 18000000
    Memory: 384
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA0
    CPU type: X86
    Task type: WEB
    Seed: 540002
}




task class:
{
    Start time: 60000
    End time : 280000
    Inter arrival: 3200
    Expected runtime: 16000000
    Memory: 256
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA1
    CPU type: X86
    Task type: WEB
    Seed: 540003
}




task class:
{
    Start time: 60000
    End time : 280000
    Inter arrival: 900
    Expected runtime: 2500000
    Memory: 64
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA2
    CPU type: X86
    Task type: WEB
    Seed: 540004
}
