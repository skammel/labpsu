#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termio.h>
#include <unistd.h>
#include <getopt.h>
#include "psu.h"
//#define DBG(f...) fprintf(stderr,f)
#define DBG(f...)

#define USAGE  " \n \
Usage: %s [options..] \n\n \
Options: \n \
--volt -v - set voltage to xx.x \n \
--amp -a - set current to xx.x \n \
--device -d - use device (/dev/ttyUSB0) \n \
--help -h - show this help \n\n"

int dev_init (char *dev);

int set_volt (int fd, double v);
int set_curr (int fd, double a);
int cmd_gmax (int devfd, double *v, double *a);
int cmd_getd (int devfd, double *v, double *a, int *s);
int cmd_gets (int devfd, double *v, double *a);
int cmd_gver (int devfd, char *ver);
int cmd_gmod (int devfd, char *model);
int rcv_data (int devfd, char *data, int len);
int parse_params (int argc, char **argv, char *dev, double *v, double *a);
int stdin_nonblock (struct termios *term_old);
void stdin_reset (struct termios term_old);
int main (int argc, char **argv)
{
  struct termios term_old;
  int devfd = -1;
  int run = 1;
  char dev[MAX_DATA_LEN] = "";
  double volt = 0.0, curr = 0.0;
  double o_volt = 0.0, o_curr = 0.0;
  double vmax = 0.0, amax = 0.0;
  char model[MAX_DATA_LEN] = "", ver[MAX_DATA_LEN] = "";
  int state = 0;

  if (parse_params (argc, argv, dev, &volt, &curr) == 1)
    return 0;

  if ((devfd = dev_init (dev[0] ? dev : DEV_DEF)) < 0)
   {
     fprintf (stderr, "cannot open %s\n", dev[0] ? dev : DEV_DEF);
     return 2;
   }

  cmd_gmax (devfd, &vmax, &amax);
  cmd_gmod (devfd, model);
  cmd_gver (devfd, ver);

  if (vmax <= 0.0 || amax <= 0.0)
   {
     fprintf (stderr, "cannot read max values\n");
     fprintf (stderr, "maybe the device is turned off\n");
     fprintf (stderr, "or it returned garbage - try again!!\n\n");
     close (devfd);
     return 3;
   }

  if (volt > vmax)
   {
     fprintf (stderr, "voltage higher than maximum %3.1f > %3.1f\n", volt,
              vmax);
     close (devfd);
     return 4;
   }

  if (curr > amax)
   {
     fprintf (stderr, "current higher than maximum %3.1f > %3.1f\n", curr,
              amax);
     close (devfd);
     return 5;
   }

  if (volt > 0.0)
    set_volt (devfd, volt);

  if (curr > 0.0)
    set_curr (devfd, curr);

  /* set stdin to none-blocking */
  fcntl (0, F_SETFL, O_NONBLOCK);
  stdin_nonblock (&term_old);
  printf ("\n Model %s %s\n", model, ver);
  printf (" max V = %3.1f - Max A = %3.1f\n\n", vmax, amax);
  printf (" + = +0.1v __ - = -0.1v __ * = +0.1a __ / = -0.1a\n");
  printf (" press <enter> to quit \n\n");
  printf ("left = gnd - right = +v\n");

  while (run)
   {
     switch (fgetc (stdin))
      {
      case '+':
        if ((volt + 0.1) <= vmax)
          volt += 0.1;
        set_volt (devfd, volt);
//   continue;
        break;
      case '-':
        if (volt > 0.1)
          volt -= 0.1;
        set_volt (devfd, volt);
//   continue;
        break;
      case '*':
        if ((curr + 0.1) < amax)
          curr += 0.1;
        set_curr (devfd, curr);
        continue;
        break;
      case '/':
        if (curr > 0.1)
          curr -= 0.1;
        set_curr (devfd, curr);
        continue;
        break;
      case '7':
        if ((curr + 1.0) < amax)
          curr += 1.0;
        set_curr (devfd, curr);
        continue;
        break;
      case '1':
        if (curr > 1.0)
          curr -= 1.0;
        set_curr (devfd, curr);
        continue;
        break;
      case '\n':
        run = 0;
        break;
      case -1:
        usleep (10000);
        break;
      default:
        fflush (stdin);
        continue;
        break;

      }
     fflush (stdin);
     cmd_gets (devfd, &volt, &curr);
     cmd_getd (devfd, &o_volt, &o_curr, &state);
     printf ("\r set: %3.1f V  %3.1f A   out: %4.2f V  %4.2f A  %s    \r",
             volt, curr, o_volt, o_curr, state ? "CC\a" : "CV");
     fflush (stdout);

     usleep (300 * 1000);

   }

  printf ("\n");
  stdin_reset (term_old);
  fflush (stdout);
  close (devfd);

  return 0;
}

int dev_init (char *dev)
{
  int devfd = -1;
  struct termios tiodata;

  devfd = open (dev, O_RDWR | O_NOCTTY);
  if (devfd < 0)
   {
     DBG ("cannot open device %s", dev);
     return devfd;
   }

  tiodata.c_cflag = (BAUD | CLOCAL | CREAD | CS8);
  tiodata.c_iflag = IGNPAR;
  tiodata.c_oflag = 0;
  tiodata.c_lflag = 0;
  tiodata.c_cc[VMIN] = 0;
  tiodata.c_cc[VTIME] = 1;      /* read timeout 1 * 10ms */

  tcflush (devfd, TCIFLUSH);    /* clean line */
/* activate new settings */
  if (tcsetattr (devfd, TCSANOW, &tiodata))
   {
     DBG ("cannot activate new settings ");
     close (devfd);
     return -1;
   }

  return devfd;
}

int rcv_data (int devfd, char *data, int len)
{
  int i = 0;
  unsigned char ch = 0;

  do
   {
     if (read (devfd, &ch, 1) != 1)
       return -1;
     else
      {
        data[i] = ch;
        DBG ("idx=%d ch=%d %c\n", i, ch, ch);
        if (ch == SEQ_ETX)
         {
           if (i == 0)
             continue;
           else
            {
              data[i + 1] = 0;
              break;
            }
         }

      }
     i++;
   }
  while (i < len);

  return 0;

}

int set_curr (int fd, double a)
{
  char cmd[MAX_DATA_LEN] = "";

  sprintf (cmd, "CURR%03.0f\r", (a * 10));
  write (fd, cmd, strlen (cmd));
  /* dummy read for OK */
  rcv_data (fd, cmd, OK_LEN);
  return 0;
}

int set_volt (int fd, double v)
{
  char cmd[MAX_DATA_LEN] = "";
  memset (cmd, 0, sizeof (cmd));
  sprintf (cmd, "VOLT%03.0f\r", (v * 10));
  write (fd, cmd, strlen (cmd));
  DBG ("v=%s ## ", cmd);
  /* dummy read for OK */
  rcv_data (fd, cmd, OK_LEN);

  return 0;
}

int cmd_gets (int devfd, double *v, double *a)
{
  char tmp[MAX_DATA_LEN] = "GETS\r", data[MAX_DATA_LEN] = "";

  memset (data, 0, sizeof (data));
  if (write (devfd, tmp, strlen (tmp)) != (int) strlen (tmp))
    return 1;

  if (!rcv_data (devfd, data, sizeof (data)))
   {
     if (strlen (data) == 7)
      {
        memset (tmp, 0, sizeof (tmp));
        memcpy (tmp, data, 3);
        *v = atof (tmp);
        *a = atof (data + 3);
        *v /= 10.0;
        *a /= 10.0;

        /* dummy read for OK */
        rcv_data (devfd, data, OK_LEN);
      }
   }

  return 0;
}

int cmd_gmod (int devfd, char *model)
{
  char tmp[MAX_DATA_LEN] = "GMOD\r", data[MAX_DATA_LEN] = "";

  memset (data, 0, sizeof (data));
  if (write (devfd, tmp, strlen (tmp)) != (int) strlen (tmp))
    return 1;

  if (!rcv_data (devfd, data, sizeof (data)))
   {
     if (strlen (data) >= 5)
      {
        memset (model, 0, sizeof (tmp));
        memcpy (model, data, strlen (data) - 1);

        /* dummy read for OK */
        rcv_data (devfd, data, OK_LEN);
      }
   }

  return 0;
}

int cmd_gver (int devfd, char *ver)
{
  char tmp[MAX_DATA_LEN] = "GVER\r", data[MAX_DATA_LEN] = "";

  memset (data, 0, sizeof (data));
  if (write (devfd, tmp, strlen (tmp)) != (int) strlen (tmp))
    return 1;

  if (!rcv_data (devfd, data, sizeof (data)))
   {
     if (strlen (data) >= 3)
      {
        memset (ver, 0, sizeof (tmp));
        memcpy (ver, data, strlen (data) - 1);

        /* dummy read for OK */
        rcv_data (devfd, data, OK_LEN);
      }
   }

  return 0;
}

int cmd_gmax (int devfd, double *v, double *a)
{
  char tmp[MAX_DATA_LEN] = "GMAX\r", data[MAX_DATA_LEN] = "";

  memset (data, 0, sizeof (data));
  if (write (devfd, tmp, strlen (tmp)) != (int) strlen (tmp))
    return 1;

  if (!rcv_data (devfd, data, sizeof (data)))
   {
     if (strlen (data) == 7)
      {
        memset (tmp, 0, sizeof (tmp));
        memcpy (tmp, data, 3);
        *v = atof (tmp);
        *a = atof (data + 3);
        *v /= 10.0;
        *a /= 10.0;

        /* dummy read for OK */
        rcv_data (devfd, data, OK_LEN);
      }
     else
       return -2;
   }

  return 0;
}

int cmd_getd (int devfd, double *v, double *a, int *s)
{
  char tmp[MAX_DATA_LEN] = "GETD\r", data[MAX_DATA_LEN] = "";

  memset (data, 0, sizeof (data));
  if (write (devfd, tmp, strlen (tmp)) != (int) strlen (tmp))
    return 1;

  if (!rcv_data (devfd, data, sizeof (data)))
   {
     if (strlen (data) == 10)
      {
        memset (tmp, 0, sizeof (tmp));
        memcpy (tmp, data, 4);
        *s = data[8] == '0' ? 0 : 1;
        data[8] = 0;
        *v = atof (tmp);
        *a = atof (data + 4);
        *v /= 100.00;
        *a /= 100.00;
        /* dummy read for OK */
        rcv_data (devfd, data, OK_LEN);
      }
   }

  return 0;
}

int parse_params (int argc, char **argv, char *dev, double *v, double *a)
{
  char c = 0;

/* options */
  struct option long_options[] = {
    {"volt", 0, 0, 'v'},
    {"amp", 1, 0, 'a'},
    {"device", 0, 0, 'd'},
    {"help", 0, 0, 'h'},
    {0, 0, 0, 0}
  };

  while (1)
   {
     int option_index = 0;

     c = getopt_long (argc, argv, "a:d:hv:", long_options, &option_index);
     if (c == -1)
       break;

     switch (c)
      {
      case 0:
        fprintf (stderr, " option %s", long_options[option_index].name);
        if (optarg)
          fprintf (stderr, " with arg %s", optarg);
        fprintf (stderr, "\n");
        break;

      case 'h':
        printf ("%s\n", VERSION);
        printf ("%s\n", AUTHOR);
        printf ("%s\n", COPY);
        printf (USAGE, argv[0]);
        return 1;

      case 'd':
        strcpy (dev, optarg);
        break;
      case 'a':
        *a = atof (optarg);
        break;
      case 'v':
        *v = atof (optarg);
        break;

      case '?':
        return 2;
        break;

      }
   }

  if (optind < argc)
   {
     fprintf (stderr, " non - option: ");
     return 2;
     while (optind < argc)
       fprintf (stderr, "%s", argv[optind++]);
     fprintf (stderr, "\n");

     return 2;
   }

  return 0;

}

int stdin_nonblock (struct termios *term_old)
{
  struct termios term_attr;
  tcgetattr (0, &term_attr);
  *term_old = term_attr;

  term_attr.c_lflag &= ~(ICANON | ECHO);
  term_attr.c_lflag &= (~ECHO);
  term_attr.c_lflag &= (~ISIG); // don't automatically handle
  term_attr.c_cc[VTIME] = 1;    // timeout (tenths of a second)
  term_attr.c_cc[VMIN] = 0;     // minimum number of characters}
  tcsetattr (0, TCSANOW, &term_attr);
  return 0;

}

void stdin_reset (struct termios term_old)
{
  tcsetattr (0, TCSANOW, &term_old);
}
