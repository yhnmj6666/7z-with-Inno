/* iscrypt.c - stand-in for Inno Setup's ISCrypt.dll.

   Inno Setup before 6.4 encrypts files (Encryption=yes) with ARCFOUR from
   ISCrypt.dll, which the compiler loads from its own folder and which is
   distributed separately. This file implements the same three functions
   (see Projects/Src/ArcFour.pas in the Inno Setup sources) so that the test
   corpus can contain encrypted installers for these versions:

     i686-w64-mingw32-gcc -O2 -shared -s -Wl,--kill-at -o ISCrypt.dll iscrypt.c

   .github/scripts/setup-inno-tools.sh builds it into the folders of the
   compilers that need it. ARCFOUR is the standard algorithm, so the
   installers are the same as with the official DLL. */

#include <stddef.h>

typedef struct
{
  unsigned char state[256];
  unsigned char x, y;
} arcfour_context; /* TArcFourContext */

__declspec(dllexport) int __stdcall ISCryptGetVersion(void)
{
  return 1;
}

__declspec(dllexport) void __stdcall ArcFourInit(arcfour_context *context,
    const void *key, unsigned key_length)
{
  const unsigned char *k = (const unsigned char *)key;
  unsigned char j = 0;
  unsigned i;
  for (i = 0; i < 256; i++)
    context->state[i] = (unsigned char)i;
  for (i = 0; i < 256; i++)
  {
    const unsigned char t = context->state[i];
    j = (unsigned char)(j + t + (key_length ? k[i % key_length] : 0));
    context->state[i] = context->state[j];
    context->state[j] = t;
  }
  context->x = 0;
  context->y = 0;
}

/* With in_buffer and out_buffer NULL, discards length bytes of key stream. */
__declspec(dllexport) void __stdcall ArcFourCrypt(arcfour_context *context,
    const void *in_buffer, void *out_buffer, unsigned length)
{
  const unsigned char *in = (const unsigned char *)in_buffer;
  unsigned char *out = (unsigned char *)out_buffer;
  unsigned char x = context->x;
  unsigned char y = context->y;
  unsigned i;
  for (i = 0; i < length; i++)
  {
    unsigned char a, b;
    x = (unsigned char)(x + 1);
    a = context->state[x];
    y = (unsigned char)(y + a);
    b = context->state[y];
    context->state[x] = b;
    context->state[y] = a;
    if (out)
      out[i] = (unsigned char)((in ? in[i] : 0) ^ context->state[(unsigned char)(a + b)]);
  }
  context->x = x;
  context->y = y;
}
