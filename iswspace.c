#include "dictP.h"

static const wint_t start [] = {
      9,     32,    133,    160,   5760,   8192,   8232,   8239,
   8287,  12288,  12288,  12288,  12288,  12288,  12288,  12288,
};

static int count [] = {
      5,      1,      1,      1,      1,     12,      2,      1,
      1,      1,      1,      1,      1,      1,      1,      1,
};

#define ARRAY_SIZE (sizeof (start) / sizeof (start [0]))

extern int iswspace__ (wint_t wc);

int iswspace__ (wint_t wc)
{
   const wint_t *l = start;
   const wint_t *r = start + ARRAY_SIZE;
   const wint_t *s = NULL;

   if (wc == WEOF)
      return 0;

   while (l < r) {
      s = l + ((r - l) >> 1);

      if (*s <= wc){
	 l = s + 1;
      }else{
	 r = s;
      }
   }

   --l;
   if (l < start)
      return 0;

   if (wc < l [0] + count [l - start])
      return 1;
   else
      return 0;
}
