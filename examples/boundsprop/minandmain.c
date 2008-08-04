#include "../../boolector.h"
#include "../../btorutil.h"
#include "minand.h"

#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char **argv)
{
  int num_bits;
  Btor *btor;
  BtorExp *formula, *zero_num_bits_m_1, *tmp, *a, *b, *c, *d, *m;
  BtorExp *result, *one, *a_and_c;

  if (argc != 2)
  {
    printf ("Usage: ./minand <num-bits>\n");
    return 1;
  }
  num_bits = atoi (argv[1]);
  if (num_bits <= 1)
  {
    printf ("Number of bits must be greater than one\n");
    return 1;
  }
  if (!btor_is_power_of_2_util (num_bits))
  {
    printf ("Number of bits must be a power of two\n");
    return 1;
  }

  btor = boolector_new ();
  boolector_set_rewrite_level (btor, 0);

  one               = boolector_one (btor, 1);
  zero_num_bits_m_1 = boolector_zero (btor, num_bits - 1);
  m                 = boolector_concat (btor, one, zero_num_bits_m_1);
  a                 = boolector_var (btor, num_bits, "a");
  b                 = boolector_var (btor, num_bits, "b");
  c                 = boolector_var (btor, num_bits, "c");
  d                 = boolector_var (btor, num_bits, "d");

  /* needed later for conclusion */
  a_and_c = boolector_and (btor, a, c);

  result = btor_minand (btor, a, b, c, d, m, num_bits);

  /* conclusion: result is indeed the minimum of a & c */
  formula = boolector_ulte (btor, result, a_and_c);
  /* we negate the formula and show that it is UNSAT */
  tmp = boolector_not (btor, formula);
  boolector_release (btor, formula);
  formula = tmp;
  boolector_dump_btor (btor, stdout, formula);

  /* clean up */
  boolector_release (btor, result);
  boolector_release (btor, formula);
  boolector_release (btor, a_and_c);
  boolector_release (btor, a);
  boolector_release (btor, b);
  boolector_release (btor, c);
  boolector_release (btor, d);
  boolector_release (btor, m);
  boolector_release (btor, zero_num_bits_m_1);
  boolector_release (btor, one);
  boolector_delete (btor);
  return 0;
}