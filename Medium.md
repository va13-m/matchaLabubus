machine class:
{
        Number of machines: 12
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 80, 40, 10, 5, 0]
        P-States: [12, 8, 6, 4]
        C-States: [12, 3, 1, 0]
        MIPS: [3000, 2400, 2000, 1500]
        GPUs: no
}


machine class:
{
        Number of machines: 4
        CPU type: X86
        Number of cores: 8
        Memory: 16384
        S-States: [120, 100, 80, 40, 10, 5, 0]
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
        MIPS: [1800, 1400, 1000, 600]
        GPUs: no
}


task class:
{
        Start time: 60000
        End time : 3600000000
        Inter arrival: 220000
        Expected runtime: 1000000
        Memory: 64
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA2
        CPU type: X86
        Task type: WEB
        Seed: 22001
}


task class:
{
        Start time: 60000
        End time : 3600000000
        Inter arrival: 350000
        Expected runtime: 1800000
        Memory: 96
        VM type: LINUX
        GPU enabled: no
        SLA type: SLA1
        CPU type: ARM
        Task type: WEB
        Seed: 22002
}


task class:
{
        Start time: 12000000
        End time :  180000000
        Inter arrival: 600000
        Expected runtime: 5000000
        Memory: 128
        VM type: LINUX
        GPU enabled: yes
        SLA type: SLA1
        CPU type: X86
        Task type: AI
        Seed: 22003
}