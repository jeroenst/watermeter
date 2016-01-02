#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <stdint.h>
#include <inttypes.h>
  
// Device is a comport like /dev/ttyUSB1
#define DEVICE "/dev/watermeter"
#define METERFILE "/usr/domotica/watermeter/waterreading"
  
double read_waterreading (const char* file_name)
{
    double i = 0;
    FILE* file = fopen (file_name, "r");
    if (file)
    {
        if (!feof(file)) fscanf (file, "%lf", &i);      
        fclose (file);        
    }
    return i;
}

void write_waterreading (const char* file_name, double waterreading)
{
    FILE* file = fopen (file_name, "w");
    fprintf (file, "%.3lf", waterreading);    
    fclose (file);        
}

// read the current level from DCD pin
int get_cts_state(int fd)
{
    int serial = 0;
    if(ioctl(fd, TIOCMGET, &serial) < 0)
    {
        printf("ioctl() failed: %d: %s\n", errno, strerror(errno));
        return -1;
    }
  
    return (serial & TIOCM_CTS) ? 1 : 0;
}
  
// sample code for blocking until DCD state changes
int main(int argc, char** argv)
{
    int omode = O_RDONLY;
    double waterreading = 0;
    waterreading = read_waterreading (METERFILE);
    
  
    // open the serial stream
    int fd = open(DEVICE, omode, 0777);
    if(fd < 0)
    {
        printf("Error opening serial device: open() failed: %d: %s\n", errno, strerror(errno));
        return -1;
    }
  
    printf("Device opened: %s\n", DEVICE);
    
  
    // detect DCD changes forever
    int i=0;
    int ctsstate= get_cts_state(fd);
    int pctsstate = 0;
    while(1)
    {
        printf("Waterreading = %.3lf, ctsstate=%d\r", waterreading, ctsstate);
        fflush(stdout);
        // block until line changes state
        if(ioctl(fd, TIOCMIWAIT, TIOCM_CTS) < 0)
        {
            printf("ioctl(TIOCMIWAIT) failed: %d: %s\n", errno, strerror(errno));
            return -1;
        }

        pctsstate = ctsstate;
        ctsstate = get_cts_state(fd);
        if ((ctsstate != pctsstate) && (ctsstate == 1))
        { 
            waterreading+=0.001;
            write_waterreading(METERFILE, waterreading);
        }
    }
}