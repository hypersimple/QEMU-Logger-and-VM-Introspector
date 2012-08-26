//defined by yuechen

typedef struct logstruct
{
    int eip; 
    int cr3;
    int eax;
    int ebx;
    int ecx;
    int edx;
    int esi;
    int edi;
    int ebp;
    int esp;
    int eflags;
    int fs;
    
    int type;
    
    int count;
    int intno;
    int error_code;
    int is_int;
    int intflag;
    int intcs;
    int inteip;
    int pc;
    int intss;
    int intesp;
    
}logstruct;
