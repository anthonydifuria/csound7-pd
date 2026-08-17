/* */
#include <sys/stat.h>

int main(int argc, char** argv)
{
  (void)argv;
#ifndef S_IRGRP
  return ((int*)(&S_IRGRP))[argc];
#else
  (void)argc;
  return 0;
#endif
}
