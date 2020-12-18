    #include <stdio.h>
    #include <fcntl.h>   /* File Control Definitions           */
    #include <sys/ioctl.h>
    #include <termios.h> /* POSIX Terminal Control Definitions */
    #include <unistd.h>  /* UNIX Standard Definitions 	   */ 
    #include <errno.h>   /* ERROR Number Definitions           */
    #include <time.h>
    #include <chrono>
    #include <iostream>
    #include <bitset>
    #include <map>
    #include <string>
    #include <cstring>



/* c_lflag bits 
#define ISIG	0000001  0x0001  Enable signals.  
#define ICANON	0000002  0x0002  Canonical input (erase and kill processing).  
#define XCASE	0000004  0x0004

#define ECHO	0000010  0x0008  Enable echo.  
#define ECHOE	0000020  0x0010  Echo erase character as error-correcting backspace.  
#define ECHOK	0000040  0x0020  Echo KILL.  
#define ECHONL	0000100  0x0040  Echo NL.  
#define NOFLSH	0000200  0x0080  Disable flush after interrupt or quit.  
#define TOSTOP	0000400  0x0100  Send SIGTTOU for background output.  
#define ECHOCTL 0001000  0x0200  If ECHO is also set, terminal special characters
			                other than TAB, NL, START, and STOP are echoed as
			                ^X, where X is the character with ASCII code 0x40
			                greater than the special character
			                (not in POSIX).  
#define ECHOPRT 0002000  0x0400  If ICANON and ECHO are also set, characters are
			                printed as they are being erased
			                (not in POSIX).  
#define ECHOKE	 0004000 0x0800  If ICANON is also set, KILL is echoed by erasing
			                each character on the line, as specified by ECHOE
			                and ECHOPRT (not in POSIX).  
#define FLUSHO	 0010000 0x1000  Output is being flushed.  This flag is toggled by
			                typing the DISCARD character (not in POSIX).  
#define PENDIN	 0040000 0x4000  All characters in the input queue are reprinted
			                when the next character is read
			                (not in POSIX).  
#define IEXTEN	0100000  0x8000  Enable implementation-defined input
			                processing.  
#define EXTPROC 0200000  0x10000  */

std::map<int, std::string> l_flag_bits = {{ISIG,"ISIG"},{ICANON,"ICANON"},{XCASE,"XCASE"},{ECHO,"ECHO"},
                                         {ECHOE,"ECHOE"},{ECHOK,"ECHOK"},{ECHONL,"ECHONL"},{NOFLSH,"NOFLSH"},
                                         {TOSTOP,"TOSTOP"},{ECHOCTL,"ECHOCTL"},{ECHOPRT,"ECHOPRT"},{ECHOKE,"ECHOKE"},
                                         {FLUSHO,"FLUSHO"},{PENDIN,"PENDIN"},{IEXTEN,"IEXTEN"},{EXTPROC,"EXTPROC"}};
int l_flag_documented = ISIG|ICANON|XCASE|ECHO|ECHOE|ECHOK|ECHONL|NOFLSH|TOSTOP|ECHOCTL|ECHOPRT|ECHOKE|FLUSHO|PENDIN|IEXTEN|EXTPROC;

/* c_iflag bits 
#define IGNBRK	0000001   Ignore break condition.  
#define BRKINT	0000002   Signal interrupt on break.  
#define IGNPAR	0000004   Ignore characters with parity errors.  
#define PARMRK	0000010   Mark parity and framing errors.  
#define INPCK	0000020   Enable input parity check.  
#define ISTRIP	0000040   Strip 8th bit off characters.  
#define INLCR	0000100   Map NL to CR on input.  
#define IGNCR	0000200   Ignore CR.
#define ICRNL	0000400   Map CR to NL on input.  
#define IUCLC	0001000   Map uppercase characters to lowercase on input
			    (not in POSIX).
#define IXON	0002000   Enable start/stop output control.
#define IXANY	0004000   Enable any character to restart output.
#define IXOFF	0010000   Enable start/stop input control.
#define IMAXBEL	0020000   Ring bell when input queue is full
			    (not in POSIX).
#define IUTF8	0040000   Input is UTF8 (not in POSIX).  */
std::map<int, std::string> i_flag_bits = {{IGNBRK,"IGNBRK"},{BRKINT,"BRKINT"},{IGNPAR,"IGNPAR"},{PARMRK,"PARMRK"},
                                         {INPCK,"INPCK"},{ISTRIP,"ISTRIP"},{INLCR,"INLCR"},{IGNCR,"IGNCR"},
                                         {ICRNL,"ICRNL"},{IUCLC,"IUCLC"},{IXON,"IXON"},{IXANY,"IXANY"},
                                         {IXOFF,"IXOFF"},{IMAXBEL,"IMAXBEL"},{IUTF8,"IUTF8"}};
int i_flag_documented = IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK|ISTRIP|INLCR|IGNCR|ICRNL|IUCLC|IXON|IXANY|IXOFF|IMAXBEL|IUTF8;

/* c_oflag bits 
#define OPOST	0000001   Post-process output.  
#define OLCUC	0000002   Map lowercase characters to uppercase on output.
			    (not in POSIX).  
#define ONLCR	0000004   Map NL to CR-NL on output.  
#define OCRNL	0000010   Map CR to NL on output.  
#define ONOCR	0000020   No CR output at column 0.  
#define ONLRET	0000040   NL performs CR function.  
#define OFILL	0000100   Use fill characters for delay.  
#define OFDEL	0000200   Fill is DEL.  
#if defined __USE_MISC || defined __USE_XOPEN
# define NLDLY	0000400   Select newline delays:  
# define   NL0	0000000   Newline type 0.  
# define   NL1	0000400   Newline type 1.  
# define CRDLY	0003000   Select carriage-return delays:  
# define   CR0	0000000   Carriage-return delay type 0.  
# define   CR1	0001000   Carriage-return delay type 1.  
# define   CR2	0002000   Carriage-return delay type 2.  
# define   CR3	0003000   Carriage-return delay type 3.  
# define TABDLY	0014000   Select horizontal-tab delays:  
# define   TAB0	0000000   Horizontal-tab delay type 0.  
# define   TAB1	0004000   Horizontal-tab delay type 1.  
# define   TAB2	0010000   Horizontal-tab delay type 2.  
# define   TAB3	0014000   Expand tabs to spaces.  
# define BSDLY	0020000   Select backspace delays:  
# define   BS0	0000000   Backspace-delay type 0.  
# define   BS1	0020000   Backspace-delay type 1.  
# define FFDLY	0100000   Select form-feed delays:  
# define   FF0	0000000   Form-feed delay type 0.  
# define   FF1	0100000   Form-feed delay type 1.  
#endif

#define VTDLY	0040000   Select vertical-tab delays:  
#define   VT0	0000000   Vertical-tab delay type 0.  
#define   VT1	0040000   Vertical-tab delay type 1.  
*/
std::map<int, std::string> o_flag_bits = {{OPOST,"OPOST"},{OLCUC,"OLCUC"},{ONLCR,"ONLCR"},{OCRNL,"OCRNL"},
                                         {ONOCR,"ONOCR"},{ONLRET,"ONLRET"},{OFILL,"OFILL"},{OFDEL,"OFDEL"},
                                         {NL1,"NL1"},{CR1,"CR1"},{CR2,"CR2"},{TAB1,"TAB1"},
                                         {TAB2,"TAB2"},{BS1,"BS1"},{FF1,"FF1"},{VT1,"VT1"}};
int o_flag_documented = OPOST|OLCUC|ONLCR|OCRNL|ONOCR|ONLRET|OFILL|OFDEL|NL1|CR1|CR2|TAB1|TAB2|BS1|FF1|VT1;

/* c_cc characters */
std::map<int, std::string> cc_names = {
    { 0, "VINTR"},
    { 1, "VQUIT"},
    { 2, "VERASE"},
    { 3, "VKILL"},
    { 4, "VEOF"},
    { 5, "VTIME"},
    { 6, "VMIN"},
    { 7, "VSWTC"},
    { 8, "VSTART"},
    { 9, "VSTOP"},
    {10, "VSUSP"},
    {11, "VEOL"},
    {12, "VREPRINT"},
    {13, "VDISCARD"},
    {14, "VWERASE"},
    {15, "VLNEXT"},
    {16, "VEOL2"}
};

std::string get_cc_name(int idx) {
    const auto& name = cc_names.find(idx);
    if (name != cc_names.end()) return name->second;
    return "BIT"+std::to_string(idx);
}

std::map<int, int> bauds = {
    {B0,0},
    {B50,50},
    {B75,75},
    {B110,110},
    {B134,134},
    {B150,150},
    {B200,200},
    {B300,300},
    {B600,600},
    {B1200,1200},
    {B1800,1800},
    {B2400,2400},
    {B4800,4800},
    {B9600,9600},
    {B19200,19200},
    {B38400,38400},
    {B57600,57600},
    {B115200,115200},
    {B230400,230400},
    {B460800,460800},
    {B500000,500000},
    {B576000,576000},
    {B921600,921600},
    {B1000000,1000000},
    {B1152000,1152000},
    {B1500000,1500000},
    {B2000000,2000000},
    {B2500000,2500000},
    {B3000000,3000000},
    {B3500000,3500000},
    {B4000000,4000000}
};

int get_baud(speed_t baud) {
    const auto& b = bauds.find(baud);
    if (b != bauds.end()) return b->second;
    std::cout<<"unknown:";
    return baud;
}

void print_flag(const std::string& header,
                unsigned int flag,
                unsigned int documented,
                const std::map<int, std::string>& bits
                ) {
    std::cout<<header;
    for (auto& it : bits) {
        if (flag & it.first) std::cout<<it.second<<' ';
    }
    int undocumented = flag & ~documented;
    if (undocumented) {
        std::cout<<"undocumented="<<std::bitset<sizeof(undocumented)*8>(flag);
    }
    std::cout<<std::endl;
}

namespace asm_termios {
    #include <asm/termios.h>

/*#define CSIZE	0000060 0x30
#define   CS5	0000000 0x00
#define   CS6	0000020 0x10
#define   CS7	0000040 0x20
#define   CS8	0000060 0x30

#define CSTOPB	0000100 0x40
#define CREAD	0000200 0x80
#define PARENB	0000400 0x100
#define PARODD	0001000 0x200
#define HUPCL	0002000 0x400
#define CLOCAL	0004000 0x800
#define CMSPAR	  010000000000	 mark or space (stick) parity 
#define CRTSCTS	  020000000000	 flow control */

    std::map<int, std::string> c_flag_bits = {{CS6,"CS6"},{CS7,"CS7"},{CSTOPB,"CSTOPB"},{CREAD,"CREAD"},
                                            {PARENB,"PARENB"},{PARODD,"PARODD"},{HUPCL,"HUPCL"},{CLOCAL,"CLOCAL"},
                                            {CMSPAR,"CMSPAR"}, {CRTSCTS,"CRTSCTS"}};
    uint32_t c_flag_documented = 0xffffffff;

    void print_termios(termios2& tc) {
        print_flag("input mode flags: ",  tc.c_iflag,i_flag_documented,i_flag_bits);
        print_flag("output mode flags: ", tc.c_oflag,o_flag_documented,o_flag_bits);
        print_flag("control mode flags: ",tc.c_cflag,c_flag_documented,c_flag_bits);
        print_flag("local mode flags: "  ,tc.c_lflag,l_flag_documented,l_flag_bits);
        std::cout<<"line discipline: "<<int(tc.c_line)<<std::endl;
        std::cout<<"control characters ";
        for (int i=0;i<NCCS; i++) std::cout<<get_cc_name(i)<<':'<<std::oct<<int(tc.c_cc[i])<<' ';
        std::cout<<std::endl;
        int cbaud = tc.c_cflag & CBAUD;
        if (cbaud == BOTHER) {
            std::cout<<"input speed "<<std::dec<<tc.c_ispeed;
            std::cout<<" output speed "<<std::dec<<tc.c_ospeed<<std::endl;
        } else {
            std::cout<<"input speed "<<std::dec<<get_baud((tc.c_cflag & CIBAUD)>>15)<<std::endl;
            std::cout<<"output speed "<<std::dec<<get_baud(cbaud)<<std::endl;
        }
        /*std::cout<<"input speed "<<std::dec<<get_baud(cfgetispeed(&tc))<<std::endl;
        std::cout<<"output speed "<<std::dec<<get_baud(cfgetospeed(&tc))<<std::endl;*/
    }

    void print_termios(int fd) {
        termios2 tc;
        int ret = ioctl(fd, TCGETS2, &tc);
        if (ret) {
            perror("tcgetattr");
            return;
        }
        print_termios(tc);
    }

    void compare_termios(int fd, termios2& src) {
        termios2 tc;
        int ret = ioctl(fd, TCGETS2, &tc);
        if (ret) {
            perror("tcgetattr");
            return;
        }
        bool equal = true;
        if (tc.c_iflag!=src.c_iflag) {
            print_flag("dst input mode flags: ",  tc.c_iflag,i_flag_documented,i_flag_bits);
            print_flag("src input mode flags: ",  src.c_iflag,i_flag_documented,i_flag_bits);
            equal = false;
        }
        if (tc.c_oflag!=src.c_oflag) {
            print_flag("dst output mode flags: ", tc.c_oflag,o_flag_documented,o_flag_bits);
            print_flag("src output mode flags: ", src.c_oflag,o_flag_documented,o_flag_bits);
            equal = false;
        }
        if (tc.c_cflag!=src.c_cflag) {
            print_flag("dst control mode flags: ",tc.c_cflag,asm_termios::c_flag_documented,asm_termios::c_flag_bits);
            print_flag("src control mode flags: ",src.c_cflag,asm_termios::c_flag_documented,asm_termios::c_flag_bits);
            equal = false;
        }
        if (tc.c_lflag!=src.c_lflag) {
            print_flag("dst local mode flags: "  ,tc.c_lflag,l_flag_documented,l_flag_bits);
            print_flag("src local mode flags: "  ,src.c_lflag,l_flag_documented,l_flag_bits);
            equal = false;
        }
        if (tc.c_line!=src.c_line) {
            std::cout<<"src line discipline: "<<int(tc.c_line)<<std::endl;
            std::cout<<"dst line discipline: "<<int(tc.c_line)<<std::endl;
            equal = false;
        }
        //std::cout<<"control characters ";
        for (int i=0;i<NCCS; i++) {
            if (tc.c_cc[i]!=src.c_cc[i]) {
                std::cout<<get_cc_name(i)<<':'<<std::oct<<int(tc.c_cc[i])<<':'<<int(src.c_cc[i])<<' ';
                equal = false;
            }
        }
        if (tc.c_ispeed!=src.c_ispeed) {
            std::cout<<"input speed "<<get_baud(tc.c_ispeed)<<':'<<get_baud(src.c_ispeed)<<std::endl;
            equal = false;
        }
        if (tc.c_ospeed!=src.c_ospeed) {
            std::cout<<"output speed "<<get_baud(tc.c_ospeed)<<':'<<get_baud(src.c_ospeed)<<std::endl;
        }
        if (equal) {
            std::cout<<"termios identical"<<std::endl;
        }
    }


    void init_termios(int fd, int baudrate) {
        print_termios(fd);
        /*---------- Setting the Attributes of the serial port using termios structure --------- */
        
        struct termios2 SerialPortSettings;

        int ret = ioctl(fd, TCGETS2, &SerialPortSettings);
        if (ret) {
            perror("tcgetattr");
            return;
        }
        SerialPortSettings.c_cflag &= ~CBAUD;
        SerialPortSettings.c_cflag |= BOTHER;
        SerialPortSettings.c_ispeed = baudrate;//921600;
        SerialPortSettings.c_ospeed = baudrate;//921600;

        // 8N1 Mode 
        SerialPortSettings.c_cflag &= ~PARENB;   // Disables the Parity Enable bit(PARENB),So No Parity
        SerialPortSettings.c_cflag &= ~CSTOPB;   // CSTOPB = 2 Stop bits,here it is cleared so 1 Stop bit
        SerialPortSettings.c_cflag &= ~CSIZE;	 // Clears the mask for setting the data size
        SerialPortSettings.c_cflag |=  CS8;      // Set the data bits = 8
        

        SerialPortSettings.c_cflag &= ~CRTSCTS;       //No Hardware flow Control
        SerialPortSettings.c_cflag |= CREAD | CLOCAL; //Enable receiver,Ignore Modem Control lines

        SerialPortSettings.c_iflag &= ~(IXON | IXOFF | IXANY);          // Disable XON/XOFF flow control both i/p and o/p
        SerialPortSettings.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Non Cannonical mode
        SerialPortSettings.c_oflag &= ~OPOST; //No Output Processing
        // Setting Time outs 
        SerialPortSettings.c_cc[VMIN] = 10; // Read at least 10 characters
        SerialPortSettings.c_cc[VTIME] = 0; // Wait indefinetly
        ret = ioctl(fd, TCSETS2, &SerialPortSettings);
        if (ret) {
            perror("tcsetattr");
            return;
        }

        print_termios(SerialPortSettings);
        compare_termios(fd,SerialPortSettings);
    }

}

void print_termios(termios& tc) {
    print_flag("input mode flags: ",  tc.c_iflag,i_flag_documented,i_flag_bits);
    print_flag("output mode flags: ", tc.c_oflag,o_flag_documented,o_flag_bits);
    print_flag("control mode flags: ",tc.c_cflag,asm_termios::c_flag_documented,asm_termios::c_flag_bits);
    print_flag("local mode flags: "  ,tc.c_lflag,l_flag_documented,l_flag_bits);
    std::cout<<"line discipline: "<<int(tc.c_line)<<std::endl;
    std::cout<<"control characters ";
    for (int i=0;i<NCCS; i++) std::cout<<get_cc_name(i)<<':'<<std::oct<<int(tc.c_cc[i])<<' ';
    std::cout<<std::endl;
    std::cout<<"input speed "<<std::dec<<get_baud(cfgetispeed(&tc))<<std::endl;
    std::cout<<"output speed "<<std::dec<<get_baud(cfgetospeed(&tc))<<std::endl;
}

void print_termios(int fd) {
    termios tc;
    int ret = tcgetattr(fd, &tc);
    if (ret) {
        perror("tcgetattr");
        return;
    }
    print_termios(tc);
}

void compare_termios(int fd, termios& src) {
    termios tc;
    int ret = tcgetattr(fd, &tc);
    if (ret) {
        perror("tcgetattr");
        return;
    }
    bool equal = true;
    if (tc.c_iflag!=src.c_iflag) {
        print_flag("dst input mode flags: ",  tc.c_iflag,i_flag_documented,i_flag_bits);
        print_flag("src input mode flags: ",  src.c_iflag,i_flag_documented,i_flag_bits);
        equal = false;
    }
    if (tc.c_oflag!=src.c_oflag) {
        print_flag("dst output mode flags: ", tc.c_oflag,o_flag_documented,o_flag_bits);
        print_flag("src output mode flags: ", src.c_oflag,o_flag_documented,o_flag_bits);
        equal = false;
    }
    if (tc.c_cflag!=src.c_cflag) {
        print_flag("dst control mode flags: ",tc.c_cflag,asm_termios::c_flag_documented,asm_termios::c_flag_bits);
        print_flag("src control mode flags: ",src.c_cflag,asm_termios::c_flag_documented,asm_termios::c_flag_bits);
        equal = false;
    }
    if (tc.c_lflag!=src.c_lflag) {
        print_flag("dst local mode flags: "  ,tc.c_lflag,l_flag_documented,l_flag_bits);
        print_flag("src local mode flags: "  ,src.c_lflag,l_flag_documented,l_flag_bits);
        equal = false;
    }
    if (tc.c_line!=src.c_line) {
        std::cout<<"src line discipline: "<<int(tc.c_line)<<std::endl;
        std::cout<<"dst line discipline: "<<int(tc.c_line)<<std::endl;
        equal = false;
    }
    //std::cout<<"control characters ";
    for (int i=0;i<NCCS; i++) {
        if (tc.c_cc[i]!=src.c_cc[i]) {
            std::cout<<get_cc_name(i)<<':'<<std::oct<<int(tc.c_cc[i])<<':'<<int(src.c_cc[i])<<' ';
            equal = false;
        }
    }
    if (tc.c_ispeed!=src.c_ispeed) {
        std::cout<<"input speed "<<get_baud(tc.c_ispeed)<<':'<<get_baud(src.c_ispeed)<<std::endl;
        equal = false;
    }
    if (tc.c_ospeed!=src.c_ospeed) {
        std::cout<<"output speed "<<get_baud(tc.c_ospeed)<<':'<<get_baud(src.c_ospeed)<<std::endl;
    }
    if (equal) {
        std::cout<<"termios identical"<<std::endl;
    }
}

void init_termios(int fd, int baudrate) {
    print_termios(fd);
    /*---------- Setting the Attributes of the serial port using termios structure --------- */
    
    struct termios SerialPortSettings;

    tcgetattr(fd, &SerialPortSettings);

    cfsetispeed(&SerialPortSettings,baudrate);//B115200); 

    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting ispeed attributes\n");

    cfsetospeed(&SerialPortSettings,baudrate); 
    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting ospeed attributes\n");

    // 8N1 Mode 
    SerialPortSettings.c_cflag &= ~PARENB;   // Disables the Parity Enable bit(PARENB),So No Parity
    SerialPortSettings.c_cflag &= ~CSTOPB;   // CSTOPB = 2 Stop bits,here it is cleared so 1 Stop bit
    SerialPortSettings.c_cflag &= ~CSIZE;	 // Clears the mask for setting the data size
    SerialPortSettings.c_cflag |=  CS8;      // Set the data bits = 8
    

    SerialPortSettings.c_cflag &= ~CRTSCTS;       //No Hardware flow Control
    SerialPortSettings.c_cflag |= CREAD | CLOCAL; //Enable receiver,Ignore Modem Control lines
    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting cflag attributes\n");

    SerialPortSettings.c_iflag &= ~(IXON | IXOFF | IXANY);          // Disable XON/XOFF flow control both i/p and o/p
    SerialPortSettings.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Non Cannonical mode
    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting iflag attributes\n");

    SerialPortSettings.c_oflag &= ~OPOST; //No Output Processing
    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting oflag attributes\n");
    
    // Setting Time outs 
    SerialPortSettings.c_cc[VMIN] = 10; // Read at least 10 characters
    SerialPortSettings.c_cc[VTIME] = 0; // Wait indefinetly
    if((tcsetattr(fd,TCSANOW,&SerialPortSettings)) != 0) //Set the attributes to the termios structure
        printf("  ERROR ! in Setting cc attributes\n");

    print_termios(SerialPortSettings);
    compare_termios(fd,SerialPortSettings);
}

void reset_uart(int fd) {
    struct termios tc = {};

    int ret = tcgetattr(fd, &tc);
    if (ret) {
        perror("tcgetattr reset");
        return;
    }

    tc.c_cflag = CREAD;

    tc.c_iflag |= BRKINT | ICRNL | IMAXBEL;
    tc.c_iflag &= ~(INLCR | IGNCR | IUTF8 | IXOFF| IUCLC | IXANY);

    tc.c_oflag |= OPOST | ONLCR;
    tc.c_oflag &= ~(OLCUC | OCRNL | ONLRET | OFILL | OFDEL | NL0 | CR0 | TAB0 | BS0 | VT0 | FF0);

    tc.c_lflag |= ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    tc.c_lflag &= ~(ECHONL | NOFLSH | XCASE | TOSTOP | ECHOPRT);

    const cc_t default_cc[] = { 03, 034, 0177, 025, 04, 0, 0, 0, 021, 023, 032, 0,
                            022, 017, 027, 026, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0 };
    static_assert(sizeof(default_cc) == sizeof(tc.c_cc), "Unknown termios struct with different size");
    memcpy(tc.c_cc, default_cc, sizeof(default_cc));
    cfsetspeed(&tc, B1200);
    ret = tcsetattr(fd, TCSANOW, &tc);
    if (ret) {
        perror("tcsetattr reset");
    }
    //return err_chk(tcsetattr(fd, TCSANOW, &tc),"tcsetaddr");
}

void init_termios_minicom(int fd, speed_t spd, bool hardware_flow=false, bool software_flow=false) {
  struct termios tty;
  tcgetattr(fd, &tty);
  cfsetospeed(&tty, spd);
  cfsetispeed(&tty, spd);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8 | CLOCAL | CREAD;
  if (hardware_flow) tty.c_cflag |= CRTSCTS;
  
  tty.c_iflag = IGNBRK;
  if (software_flow) tty.c_iflag |= IXON | IXOFF;
  
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 5;
  tcsetattr(fd, TCSANOW, &tty);
}

int main(void)
    {
    int fd;/*File Descriptor*/
    
    printf("\n +----------------------------------+");
    printf("\n |        Serial Port Read          |");
    printf("\n +----------------------------------+\n");

    /*------------------------------- Opening the Serial Port -------------------------------*/

    /* Change /dev/ttyUSB0 to the one corresponding to your system */

        fd = open("/dev/ttyS1",O_RDWR | O_NOCTTY);	/* ttyUSB0 is the FT232 based USB2SERIAL Converter   */
                               /* O_RDWR   - Read/Write access to serial port       */
                            /* O_NOCTTY - No terminal will control the process   */
                            /* Open in blocking mode,read will wait              */
                                
                                                                        
                                
        if(fd == -1)						/* Error Checking */
               printf("  Error! in Opening port  \n");
        else
               printf("  port Opened Successfully \n");

    //reset_uart(fd);
    //asm_termios::init_termios(fd,115200);
    //init_termios_minicom(fd,B115200);
    init_termios_minicom(fd,B921600);
    //init_termios(fd, B115200);
    //asm_termios::init_termios(fd,921600);
    //init_termios(fd, B921600);

        /*------------------------------- Read data from serial port -----------------------------*/

    int ret = tcflush(fd, TCIFLUSH);   /* Discards old data in the rx buffer            */
    if (ret) {
        perror("flush port");
    }

    int len = 2048;
    char read_buffer[2048];   /* Buffer to store the data received              */
    
    auto start = std::chrono::high_resolution_clock::now();
    int all = 0;
    while(true) {
        int bytes_read = read(fd,&read_buffer,2048); /* Read the data                   */
        all+=bytes_read;
        printf("  Bytes Rxed - %d\n", bytes_read); /* Print the number of bytes read */
        //printf("\n\n  ");

        //for(int i=0;i<bytes_read;i++)	 /*printing only the received characters*/
        //    printf("%c",read_buffer[i]);

        //printf("\n +----------------------------------+\n\n\n");
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        if (elapsed.count()>1) {
            printf("  Bytes all - %d in %f\n", all, elapsed.count());
            break;
        }
    }

    close(fd); /* Close the serial port */
    return 0;
    }