#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <unistd.h>
#include <mysql.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>  
#include <sys/wait.h>



// Device is a comport like /dev/ttyUSB1
#define DEVICE "/dev/watermeter"
#define METERFILE "/usr/domotica/watermeter/waterreading"


#define BUFFER_SIZE 1024
#define on_error(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(1); }



int writetodatabase(double waterreading_m3, double waterflow_m3h) 
{
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;

	char *server = "localhost";
	char *user = "casaan";
	char *password = "casaan"; /* set me first */
	char *database = "casaan";

	conn = mysql_init(NULL);

	/* Connect to database */
	if (!mysql_real_connect(conn, server,
				user, password, database, 0, NULL, 0)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	/* send SQL query */

	char sqlquerystring[180];
	sprintf (sqlquerystring, "INSERT INTO watermeter (m3, m3h) VALUES (%.3lf,%.3lf);", waterreading_m3, waterflow_m3h);
	if (mysql_query(conn, sqlquerystring)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}

	res = mysql_use_result(conn);

	/* output table name */
	//   printf("MySQL Tables in mysql database:\n");
	//   while ((row = mysql_fetch_row(res)) != NULL)
	//      printf("%s \n", row[0]);

	/* close connection */
	mysql_free_result(res);
	mysql_close(conn);

	return 0;
}

int create_tcpserver()
{
	int port = 58882;
	int server_fd, err;
	
	struct sockaddr_in server, client;
	char buf[BUFFER_SIZE];

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) on_error("Could not create socket\n");

	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);

	int opt_val = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);

	err = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
	if (err < 0) on_error("Could not bind socket\n");

	err = listen(server_fd, 128);
	if (err < 0) on_error("Could not listen on socket\n");

	printf("TCP Server is listening on %d\n", port);

	return server_fd;
}

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


int   main(int argc, char * argv[])
{
	double waterreading_m3 = -1;
	waterreading_m3 = read_waterreading (METERFILE);
	double waterflow_m3h = -1;

	int pipefd[2];
	pid_t cpid;
	char buf;

	pipe(pipefd); // create the pipe
	cpid = fork(); // duplicate the current process
	if (cpid == 0) // if I am the child then
	{
		// Child is worker for TCP connections and database writes
		close(pipefd[1]); // close the write-end of the pipe, I'm not going to use it
		
		// Initialize TCP server
		int server_fd, client_fd, err;
		struct sockaddr_in server, client;
		server_fd = create_tcpserver();
	

		fd_set set;
		struct timeval timeout;
		/* Initialize the file descriptor set. */
		FD_ZERO (&set);
		FD_SET (server_fd, &set);
		FD_SET (pipefd[0], &set);
		

		/* Initialize the timeout data structure. */
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		/* select returns 0 if timeout, 1 if input available, -1 if error. */
		while(1)
		{
			FD_ZERO (&set);
			FD_SET (server_fd, &set);
			FD_SET (pipefd[0], &set);
			if (client_fd > 0) FD_SET(client_fd, &set);
			
			select (FD_SETSIZE,&set, NULL, NULL, &timeout);
			
			if (FD_ISSET(server_fd, &set))
			{
				// Connection made on TCP socket!
				socklen_t client_len = sizeof(client);
				client_fd = accept(server_fd, (struct sockaddr *) &client, &client_len);
				if (client_fd < 0) printf("Could not establish new connection\n");
				else
				{
					printf ("Tcp client connected!");
					char json[80];
					if (waterflow_m3h >= 0)
					{
						sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":%.3lf},\"total\":{\"m3\":%.3lf}}}", waterflow_m3h, waterreading_m3);
					}
					else
					{
						sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":null},\"total\":{\"m3\":null}}}");
					}
					write (client_fd,json,strlen(json));
				}
			}      
			if (FD_ISSET(client_fd, &set))
			{
				// Received message from tcp client!
				char msg[80];
				bzero(msg, 80);
				int n = read(client_fd, msg, 80);
				if (n < 0)
				{
					// Connection was closed
					close(client_fd);
				}
				else
				{
					printf("server received %d bytes: %s", n, msg);
					if (strcmp(msg, "getwatermeter") == 0)
					{
						char json[80];
						if (waterflow_m3h >= 0)
						{
							sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":%.3lf},\"total\":{\"m3\":%.3lf}}}", waterflow_m3h, waterreading_m3);
						}
						else
						{
							sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":null},\"total\":{\"m3\":null}}}");
						}
						write (client_fd,json,strlen(json));
					}
				}
			}    
			
			if (FD_ISSET(pipefd[0], &set))
			{
				// Received new watermeter values from Parent!
				char msg[80];
				bzero(msg, 80);
				read(pipefd[0], &msg, 80);
				int ctsstate;
				sscanf(msg, "%lf %lf %d", &waterreading_m3, &waterflow_m3h, &ctsstate);
				writetodatabase(waterreading_m3, waterflow_m3h);    

				char json[80];
				if (waterflow_m3h >= 0)
				{
					sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":%.3lf},\"total\":{\"m3\":%.3lf}}}", waterflow_m3h, waterreading_m3);
				}
				else
				{
					sprintf (json, "{\"watermeter\":{\"now\":{\"m3h\":null},\"total\":{\"m3\":null}}}");
				}
				if (client_fd >= 0) write (client_fd,json,strlen(json));
			}
		}

		close(pipefd[0]); // close the read-end of the pipe
		exit(EXIT_SUCCESS);
	}
	else 
	// ##### THIS IS THE PARENT THAT READS THE PULSES FROM THE WATERMETER AND INFORMS THE CLIENT ####
	{
		// Parent does reading the water meter
		close(pipefd[0]); // close the read-end of the pipe, I'm not going to use it

		int omode = O_RDONLY;


		// open the serial stream
		int fd;
		while (fd = open(DEVICE, omode, 0777) < 0)
		{
			printf("Error opening serial device: open() failed: %d: %s\n", errno, strerror(errno));
			sleep(1);
		}

		printf("Device opened: %s\n", DEVICE);


		// detect DCD changes forever
		int i=0;
		int ctsstate= get_cts_state(fd);
		int pctsstate = ctsstate;

		struct timespec spec;
		clock_gettime(CLOCK_REALTIME, &spec);
		long tv_nsecold = spec.tv_nsec ;
		while(1)
		{
			printf("Waterreading_m3 = %.3lf, Waterflow_m3h= %.31f, ctsstate=%d\r", waterreading_m3, waterflow_m3h,  ctsstate);
			fflush(stdout);

			// block until line changes state
			if(ioctl(fd, TIOCMIWAIT, TIOCM_CTS) < 0)
			{
				printf("ioctl(TIOCMIWAIT) failed: %d: %s\n", errno, strerror(errno));
				return -1;
			}

			pctsstate = ctsstate;
			ctsstate = get_cts_state(fd);
			if ((ctsstate != pctsstate))
			{
				// Calculate waterflow
				clock_gettime(CLOCK_REALTIME, &spec);
				long ms = round((spec.tv_nsec - tv_nsecold) / 1.0e6);
				waterflow_m3h = 0.0005 * (1000 / ms) * 3600;
				
				// Calculate waterreading
				waterreading_m3+=0.0005;
				
				// Write waterreading to file
				write_waterreading(METERFILE, waterreading_m3);
				
				// Send values to child
				char msg[80];
				sprintf (msg,"%.3lf %.3lf %d", waterreading_m3, waterflow_m3h, ctsstate);
				write(pipefd[1], msg, strlen(msg)); // send values to child
			}
		}

		close(pipefd[1]); // close the write-end of the pipe, thus sending EOF to the reader
		wait(NULL); // wait for the child process to exit before I do the same
		exit(EXIT_SUCCESS);
	}
	return 0;
}
