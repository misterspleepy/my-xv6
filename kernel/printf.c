//
// formatted console output -- printf, panic.
//

#include <stdarg.h>
#include "riscv.h"

volatile int panicked = 0;

static char digits[] = "0123456789abcdef";
void uartputc_sync(int c);
static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;
  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uartputc_sync(buf[i]);
}

void
printptr(uint64 x)
{
  int i;
  uartputc_sync('0');
  uartputc_sync('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    uartputc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c;
  char *s;

  if (fmt == 0)
    return;

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      uartputc_sync(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uartputc_sync(*s);
      break;
    case '%':
      uartputc_sync('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      uartputc_sync('%');
      uartputc_sync(c);
      break;
    }
  }
  va_end(ap);
}