#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <stdio.h>

#include "llc.h"


static
std::string read_string (FILE *f)
{
  std::string s;
  unsigned int len = (unsigned char)getc (f);
  s.resize (len, ' ');
  for (int i = 0; i < (int)s.length (); i++)
    s[i]=getc (f);
  return s;
}

static void
skip (FILE *f, int l)
{
	for (int i = 0; i < l; i++)
	  getc(f);
}

static uint16_t
read_uint16 (FILE *f)
{
  uint16_t ret = ((uint16_t)(unsigned char)getc (f))
    		 + (((uint16_t)(unsigned char)getc (f)) << 8);
  return ret;
}

static uint16_t
read_uint32 (FILE *f)
{
  uint16_t ret = ((uint32_t)(unsigned char)read_uint16 (f))
    		 + (((uint32_t)(unsigned char)read_uint16 (f)) << 16);
  return ret;
}

llc *
llc::load(FILE *f, bool verbose)
{
	std::string s = read_string (f);
	if (s != "XCon")
	  {
	    fprintf (stderr, "Expected Xcon and got %s\n", s.c_str ());
	    return NULL;
	  }
	/* Unknown stuff.  */
	skip (f, 9);
	s = read_string (f);
	if (s != "TYPE")
	  {
	    fprintf (stderr, "Expected TYPE and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t type = read_uint16 (f);
	if (verbose)
	  printf ("Type: %i\n", type);
	s = read_string (f);
	if (s != "CaptureOne LCC")
	  {
	    fprintf (stderr, "Expected CaptureOne LLC and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t llc = read_uint16 (f);
	if (verbose)
	  printf ("CaptureOne LLC: %i\n", llc);
	s = read_string (f);
	if (s != "VER")
	  {
	    fprintf (stderr, "Expected VER and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t v1 = read_uint16 (f);
	uint16_t v2 = read_uint16 (f);
	uint16_t v3 = read_uint16 (f);
	getc(f);
	if (verbose)
	  printf ("VER: %i %i %i\n", v1, v2, v3);
	s = read_string (f);
	if (s != "Camera")
	  {
	    fprintf (stderr, "Expected Camera and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t camera = read_uint16 (f);
	uint8_t val = getc (f);
	if (verbose)
	  printf ("Camera: %i %i\n", camera, val);
	s = read_string (f);
	if (s != "Make")
	  {
	    fprintf (stderr, "Expected Make and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t make = read_uint16 (f);
	s = read_string (f);
	uint16_t make2 = read_uint16 (f);
	if (verbose)
	  printf ("Make: %i %s %i\n", make, s.c_str (), make2);
	s = read_string (f);
	if (s != "Model")
	  {
	    fprintf (stderr, "Expected Model and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t model = read_uint16 (f);
	s = read_string (f);
	uint16_t model2 = read_uint16 (f);
	if (verbose)
	  printf ("Model: %i %s %i\n", model, s.c_str (), model2);
	s = read_string (f);
	if (s != "S/N")
	  {
	    fprintf (stderr, "Expected S/N and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t sn = read_uint16 (f);
	s = read_string (f);
	uint16_t sn2 = read_uint16 (f);
	if (verbose)
	  printf ("S/N: %i %s %i\n", sn, s.c_str (), sn2);
	skip (f, 5);
	s = read_string (f);
	if (s != "RAW")
	  {
	    fprintf (stderr, "Expected RAW and got %s %li\n", s.c_str (), s.length ());
	    return NULL;
	  }
	skip (f, 20);
	s = read_string (f);
	if (s != "hash")
	  {
	    fprintf (stderr, "Expected hash and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 23);
	s = read_string (f);
	if (s != "Lens")
	  {
	    fprintf (stderr, "Expected Lens and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 3);
	s = read_string (f);
	if (s != "Par")
	  {
	    fprintf (stderr, "Expected Par and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 20);
	s = read_string (f);
	if (s != "Shift")
	  {
	    fprintf (stderr, "Expected Shift and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 18);
	s = read_string (f);
	if (s != "Chroma")
	  {
	    fprintf (stderr, "Expected Chroma and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t chroma = read_uint16 (f);
	if (verbose)
	  printf ("Chroma: %i\n", chroma);
	s = read_string (f);
	if (s != "REF")
	  {
	    fprintf (stderr, "Expected REF and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 7);
	s = read_string (f);
	if (s != "Hdr")
	  {
	    fprintf (stderr, "Expected Hdr and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 20);
	s = read_string (f);
	if (s != "RGBMean")
	  {
	    fprintf (stderr, "Expected RGBMean and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint32_t r = read_uint32 (f);
	uint32_t g = read_uint32 (f);
	uint32_t b = read_uint32 (f);
	if (verbose)
	  printf ("RGB mean: %i %i %i\n", r, g, b);
	s = read_string (f);
	if (s != "RBTable")
	  {
	    fprintf (stderr, "Expected RGBTable and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t rbtable = read_uint16 (f);
	if (verbose)
	  printf ("RB table: %i\n", rbtable);
	s = read_string (f);
	if (s != "REF")
	  {
	    fprintf (stderr, "Expected REF and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 11);
	s = read_string (f);
	if (s != "LightFalloff")
	  {
	    fprintf (stderr, "Expected LifhtFalloff and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t ligthfalloff = read_uint16 (f);
	if (verbose)
	  printf ("LifghtFalloff: %i\n", ligthfalloff);
	s = read_string (f);
	if (s != "REF")
	  {
	    fprintf (stderr, "Expected REF and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 9);
	s = read_string (f);
	if (s != "Hdr")
	  {
	    fprintf (stderr, "Expected Hdr and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 22);
	s = read_string (f);
	if (s != "Model")
	  {
	    fprintf (stderr, "Expected Model and got %s\n", s.c_str ());
	    return NULL;
	  }
	uint16_t model3 = read_uint16 (f);
	if (verbose)
	  printf ("Model: %i\n", model);
	s = read_string (f);
	if (s != "REF")
	  {
	    fprintf (stderr, "Expected REF and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	skip (f, 13);
	s = read_string (f);
	if (s != "DAT")
	  {
	    fprintf (stderr, "Expected DAT and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t dat1 = read_uint16 (f);
	uint16_t dat2 = read_uint16 (f);
	if (verbose)
	  printf ("Dat: %i %i\n", dat1, dat2);
	s = read_string (f);
	if (s != "BIN")
	  {
	    fprintf (stderr, "Expected BIN and got >%s<\n", s.c_str ());
	    return NULL;
	  }
	uint16_t bin = read_uint16 (f);
	//printf ("%i\n", bin);
	uint16_t bin2 = read_uint16 (f);
	//printf ("%i\n", bin2);
	uint16_t bin3 = read_uint16 (f);
	//printf ("%i\n", bin3);
	uint16_t bin4 = read_uint16 (f);
	//printf ("%i\n", bin4);
	class llc *llci = new class llc ();
	llci->alloc (111, 84);
	for (int y = 0; y < 84; y++)
	  {
	    for (int x = 0; x < 111; x++)
	      {
		    uint16_t val = read_uint16 (f);
		    uint16_t val2 = read_uint16 (f);
		    float weight = ((luminosity_t)val)/32668;
		    float weight2 = ((luminosity_t)val2)/32668;
		    llci->set_weight(x, y, /*(((luminosity_t)val) / 0x8000-1)*30 + 1*/ (1/weight)*weight2);
#if 0
		for (int c = 0; c < 2; c++)
		  {
		    uint16_t val = read_uint16 (f);
		    if ((c%6)%2 == 0)
		      continue;
		    llci->set_weight(x, y, /*(((luminosity_t)val) / 0x8000-1)*30 + 1*/ ((luminosity_t)val)/32668);
		  }
#endif
	      }
	  }
#if 0
	FILE *out = fopen ("test.pnm", "w");
	fprintf (out, "P2\n 111 84\n65535\n");
	//fprintf (out, "P3\n 37 84\n65535\n");
	for (int j = 0; j < 84; j++)
	{
	  for (int j = 0; j < 74/2; j++)
	    {
	      for (int c = 0; c < 6; c++)
	      {
		      uint16_t val = read_uint16 (f);
		      if ((c%6)%2 == 1)
			      continue;
		      if (val/256>128)
			printf ("+");
		      else
			printf (" ");
		      fprintf(out, " %i", val);
	      }
	    }
	  fprintf(out, "\n");
	  printf("\n");
	}
#endif

	return llci;
}
#if 0
int
main()
{
	decode (stdin, true);
	return 0;
}
#endif
