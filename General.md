machine class:
{
        Number of machines: 16
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 100, 80, 40, 10, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
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
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: yes
}


machine class:
{
        Number of machines: 8
        CPU type: ARM
        Number of cores: 8
        Memory: 12288
        S-States: [80, 60, 40, 20, 10, 5, 0]
        P-States: [8, 4, 2, 1]
        C-States: [8, 2, 1, 0]
        MIPS: [2000, 1500, 1200, 800]
        GPUs: no
}


machine class:
{
        Number of machines: 4
        CPU type: POWER
        Number of cores: 32
        Memory: 131072
        S-States: [120, 60, 30, 15, 8, 4, 0]
        P-States: [8, 4, 2, 1]
        C-States: [8, 2, 1, 0]
        MIPS: [1500, 1200, 1000, 800]
        GPUs: no
}


task class:
{
        Start time: 60000
        End time : 3600000000
        Inter arrival: 180000
        Expected runtime: 1000000
        Memory: 64
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 33001
}


task class:
{
        Start time: 9000000
        End time : 3600000000
        Inter arrival: 280000
        Expected runtime: 1400000
        Memory: 96
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: ARM
        Task type: WEB
        Seed: 33002
}


task class:
{
        Start time: 15000000
        End time :  800000000
        Inter arrival: 550000
        Expected runtime: 4500000
        Memory: 128
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: X86
        Task type: AI
        Seed: 33003
}


task class:
{
        Start time: 5000000
        End time :  7000000
        Inter arrival: 10000
        Expected runtime: 6000000
        Memory: 8192
        VM type: AIX
        GPU enabled: no
        SLA type: SLA3
        CPU type: POWER
        Task type: HPC
        Seed: 33004
}


task class:
{
        Start time: 25000000
        End time :  27000000
        Inter arrival: 8000
        Expected runtime: 6000000
        Memory: 8192
        VM type: AIX
        GPU enabled: no
        SLA type: SLA3
        CPU type: POWER
        Task type: HPC
        Seed: 33005
}


task class:
{
        Start time: 22000000
        End time :  35000000
        Inter arrival: 120000
        Expected runtime: 2000000
        Memory: 64
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA0
        CPU type: X86
        Task type: WEB
        Seed: 33006
}
