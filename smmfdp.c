#define _GNU_SOURCE

#include <math.h>

#include "bwt.h"
#include "map.h"
#include "divsufsort.h"
#include "sesame.h"

#define GAMMA 17
#define PROBDEFAULT 0.01

// ------- Machine-specific code ------- //
// The 'mmap()' option 'MAP_POPULATE' is available
// only on Linux and from kern version 2.6.23.
#if __linux__
  #include <linux/version.h>
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
    #define _MAP_POPULATE_AVAILABLE
  #endif
#endif

#ifdef _MAP_POPULATE_AVAILABLE
  #define MMAP_FLAGS (MAP_PRIVATE | MAP_POPULATE)
#else
  #define MMAP_FLAGS MAP_PRIVATE
#endif


// ------- Machine-specific code ------- //
// The 'mmap()' option 'MAP_POPULATE' is available
// only on Linux and from kern version 2.6.23.
#if __linux__
  #include <linux/version.h>
  #if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
    #define _MAP_POPULATE_AVAILABLE
  #endif
#endif

#ifdef _MAP_POPULATE_AVAILABLE
  #define MMAP_FLAGS (MAP_PRIVATE | MAP_POPULATE)
#else
  #define MMAP_FLAGS MAP_PRIVATE
#endif


// Error-handling macro.
#define exit_cannot_open(x) \
   do { fprintf(stderr, "cannot open file '%s' %s:%d:%s()\n", (x), \
         __FILE__, __LINE__, __func__); exit(EXIT_FAILURE); } while(0)

#define exit_error(x) \
   do { if (x) { fprintf(stderr, "error: %s %s:%d:%s()\n", #x, \
         __FILE__, __LINE__, __func__); exit(EXIT_FAILURE); }} while(0)

typedef struct uN0_t uN0_t;
typedef struct seedp_t seedp_t;

struct uN0_t {
   double u;
   size_t N0;
};

struct seedp_t {
   double off;
   double nul;
};

static double PROB = PROBDEFAULT;

void
build_index
(
   const char * fname
)
{

   // Open fasta file.
   FILE * fasta = fopen(fname, "r");
   if (fasta == NULL) exit_cannot_open(fname);

   char buff[256];

   // Read and normalize genome
   sprintf(buff, "%s.chr", fname);
   fprintf(stderr, "reading genome... ");
   char * genome = normalize_genome(fasta, buff);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating suffix array... ");
   int64_t * sa = compute_sa(genome);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating bwt... ");
   bwt_t * bwt = create_bwt(genome, sa);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "creating Occ table... ");
   occ_t * occ = create_occ(bwt);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "filling lookup table... ");
   lut_t * lut = malloc(sizeof(lut_t));
   fill_lut(lut, occ, (range_t) {.bot=1, .top=strlen(genome)}, 0, 0);
   fprintf(stderr, "done.\n");

   fprintf(stderr, "compressing suffix array... ");
   csa_t * csa = compress_sa(sa);
   fprintf(stderr, "done.\n");

   // Write files
   char * data;
   ssize_t ws;
   size_t sz;


   // Write the compressed suffix array file.
   sprintf(buff, "%s.sa", fname);
   int fsar = creat(buff, 0644);
   if (fsar < 0) exit_cannot_open(buff);
   
   ws = 0;
   sz = sizeof(csa_t) + csa->nint64 * sizeof(int64_t);
   data = (char *) csa;
   while (ws < sz) ws += write(fsar, data + ws, sz - ws);
   close(fsar);


   // Write the Burrows-Wheeler transform.
   sprintf(buff, "%s.bwt", fname);
   int fbwt = creat(buff, 0644);
   if (fbwt < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(bwt_t) + bwt->nslots * sizeof(uint8_t);
   data = (char *) bwt;
   while (ws < sz) ws += write(fbwt, data + ws, sz - ws);
   close(fbwt);


   // Write the Occ table.
   sprintf(buff, "%s.occ", fname);
   int focc = creat(buff, 0644);
   if (focc < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(occ_t) + occ->nrows * SIGMA * sizeof(blocc_t);
   data = (char *) occ;
   while (ws < sz) ws += write(focc, data + ws, sz - ws);
   close(focc);


   // Write the lookup table
   sprintf(buff, "%s.lut", fname);
   int flut = creat(buff, 0644);
   if (flut < 0) exit_cannot_open(buff);

   ws = 0;
   sz = sizeof(lut_t);
   data = (char *) lut;
   while (ws < sz) ws += write(flut, data + ws, sz - ws);
   close(flut);

   // Clean up.
   free(csa);
   free(bwt);
   free(occ);
   free(lut);

}


index_t
load_index
(
   const char  * fname
)
{

   size_t mmsz;
   char buff[256];

   chr_t * chr = index_load_chr(fname);
   exit_error(chr == NULL);

   
   sprintf(buff, "%s.sa", fname);
   int fsar = open(buff, O_RDONLY);
   if (fsar < 0) exit_cannot_open(buff);

   mmsz = lseek(fsar, 0, SEEK_END);
   csa_t *csa = (csa_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, fsar, 0);
   exit_error(csa == NULL);
   close(fsar);


   sprintf(buff, "%s.bwt", fname);
   int fbwt = open(buff, O_RDONLY);
   if (fbwt < 0) exit_cannot_open(buff);

   mmsz = lseek(fbwt, 0, SEEK_END);
   bwt_t *bwt = (bwt_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, fbwt, 0);
   exit_error(bwt == NULL);
   close(fbwt);


   sprintf(buff, "%s.occ", fname);
   int focc = open(buff, O_RDONLY);
   if (focc < 0) exit_cannot_open(buff);

   mmsz = lseek(focc, 0, SEEK_END);
   occ_t *occ = (occ_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, focc, 0);
   exit_error(occ == NULL);
   close(focc);


   sprintf(buff, "%s.lut", fname);
   int flut = open(buff, O_RDONLY);
   if (flut < 0) exit_cannot_open(buff);

   mmsz = lseek(flut, 0, SEEK_END);
   lut_t *lut = (lut_t *) mmap(NULL, mmsz, PROT_READ, MMAP_FLAGS, focc, 0);
   exit_error(lut == NULL);
   close(flut);

   return (index_t) {.chr = chr, .csa = csa, .bwt = bwt,
      .occ = occ, .lut = lut };

}


uN0_t
estimate_uN0
(
   const char    * seq,
   const index_t   idx
)
{

   const size_t n = 21;
   const double MU[3] = { .06, .04, .02 };

   size_t L1, L2, R1, R2;
   L1 = L2 = R1 = R2 = 0;
   int mlen;

   size_t merid = 0;

   // Look up the beginning (reverse) of the query in lookup table.
   for (mlen = 0 ; mlen < LUTK ; mlen++) {
      // Note: every "N" is considered a "A".
      uint8_t c = ENCODE[(uint8_t) seq[30-mlen-1]];
      merid = c + (merid << 2);
   }
   range_t range = idx.lut->kmer[merid];

   for ( ; mlen < 30 ; mlen++) {
      // When there are "N" in the reference, the estimation
      // must bail out because we cannot find the answer.
      if (NONALPHABET[(uint8_t) seq[30-mlen-1]])
         goto in_case_of_failure;
      int c = ENCODE[(uint8_t) seq[30-mlen-1]];
      range.bot = get_rank(idx.occ, c, range.bot - 1);
      range.top = get_rank(idx.occ, c, range.top) - 1;
      // If only 1 hit remains, we can bail out.
      if (range.bot == range.top) break;
      if (mlen == 20) {
         L1 = range.top - range.bot;
         L2 = n * L1;
      }
      if (mlen > 20) {
         L2 += range.top - range.bot;
      }
   }

   L1 -= (range.top - range.bot);

   merid = 0;

   // Look up the beginning (forward) of the query in lookup table.
   for (mlen = 0 ; mlen < LUTK ; mlen++) {
      // Note: every "N" is considered a "T".
      uint8_t c = REVCMP[(uint8_t) seq[mlen]];
      merid = c + (merid << 2);
   }
   range = idx.lut->kmer[merid];

   for ( ; mlen < 30 ; mlen++) {
      // When there are "N" in the reference, the estimation
      // must bail out because we cannot find the answer.
      if (NONALPHABET[(uint8_t) seq[mlen]])
         goto in_case_of_failure;
      int c = REVCMP[(uint8_t) seq[mlen]];
      range.bot = get_rank(idx.occ, c, range.bot - 1);
      range.top = get_rank(idx.occ, c, range.top) - 1;
      // If only 1 hit remains, we can bail out.
      if (range.bot == range.top) break;
      if (mlen == 20) {
         R1 = range.top - range.bot;
         R2 = n * R1;
      }
      if (mlen > 20) {
         R2 += range.top - range.bot;
      }
   }

   R1 -= (range.top - range.bot);

   double loglik = -INFINITY;
   size_t best_N0 = 0;
   double best_mu = 0.0;

   double C[3] = {
       (1 - pow(1-.06,n)) / (n * pow(1-.06,n-1)),
       (1 - pow(1-.04,n)) / (n * pow(1-.04,n-1)),
       (1 - pow(1-.02,n)) / (n * pow(1-.02,n-1)),
   };

   for (int iter = 0 ; iter < 3 ; iter++) {

      double mu = MU[iter];

      double L3 = L2 / (1-mu) - L1 / mu;
      double R3 = R2 / (1-mu) - R1 / mu;

      // Compute the number of duplicates.
      double N0 = (L1+R1 + C[iter]*(L3+R3)) / 2.0;
      if (N0 < 1.0) N0 = 1.0;

      double tmp = 2 * lgamma(N0+1) + (L1+R1) * log(mu) +
         (L2+R2) * log(1-mu) + (2*N0-L1-R1) * log(1-pow(1-mu,n)) -
         (lgamma(N0-L1+1) + lgamma(N0-R1+1));

      if (tmp < loglik) {
         break;
      }

      loglik = tmp;
      best_N0 = round(N0);
      best_mu = mu;

   }

   return (uN0_t) { best_mu, best_N0 };

in_case_of_failure:
   // Return an impossible value.
   return (uN0_t) { 0.0 / 0.0, -1 };

}


int
cmpN0
(
   const void * a,
   const void * b
)
{
   uN0_t A = *(uN0_t *) a;
   uN0_t B = *(uN0_t *) b;

   return (A.N0 > B.N0) - (A.N0 < B.N0);

}


seedp_t
quality
(
         aln_t   aln,
   const char *  seq,
         index_t idx,
         int     skip
)
{

   double slen = strlen(seq);
   // FIXME: assert is very weak (here only
   // to declare 'uN0' on stack).
   assert(slen < 250);

   uN0_t uN0[50];

   // Assume the worst (many similar duplicates).
   const double worst_case_mu = 0.02;
   const size_t worst_case_N0 = 64000;

   int tot = 0;
   for (int s = 0 ; s <= slen-30 ; s += 10, tot++) {
      //uN0[tot] = estimate_uN0(aln.refseq + s, idx);
      uN0[tot] = estimate_uN0(aln.refseq + s, idx);
      // Case that estimation failed (e.g., because of "N").
      if (uN0[tot].u != uN0[tot].u) {
         uN0[tot] = (uN0_t) { worst_case_mu, worst_case_N0 };
      }
   }

   // Find min/max N0.
   int minN0 = 64000;
   int maxN0 = 0;
   double minu = 1.0;
   for (int i = 0 ; i < tot ; i++) {
      if (uN0[i].N0 > maxN0) maxN0 = uN0[i].N0;
      if (uN0[i].N0 < minN0) minN0 = uN0[i].N0;
      if (uN0[i].u < minu) minu = uN0[i].u;
   }

   if (maxN0 < 10 * minN0 || maxN0 < 20) {
      // All the estimates of N0 are similar. Take the median.
      qsort(uN0, tot, sizeof(uN0_t), cmpN0);

      double best_mu = uN0[tot/2].u;
      double best_N0 = uN0[tot/2].N0;

      double poff;
      double pnul;

      // Probability of a purely random hit.
      double P = 1-exp(-(slen-GAMMA) * (idx.chr->gsize) / pow(4,GAMMA));
      if (skip == 0) {
         poff = auto_mem_seed_offp(slen, best_mu, best_N0);
         pnul = auto_mem_seed_nullp(slen, best_mu, best_N0) * P;
      }
      else {
         poff = auto_skip_seed_offp(slen, skip, best_mu, best_N0);
         pnul = auto_skip_seed_nullp(slen, skip, best_mu, best_N0) * P;
      }

      if (maxN0 <= 1 && minu == 0.06) {
         // Special "high confidence" case. On every segment, the
         // estimate of N0 is 1. However, this minimum value is
         // pessimistic because it is also possible that N0 is 0,
         // in which case seeding cannot be off target. So the
         // probability that we are looking for is the probability
         // that N0 is really equal to 1, times the probability that
         // MEM seeding is off target.
         //
         // Since u = 0.06, each 10-bp window contains a difference
         // with probability 0.461. If there is a difference in the
         // central window of each 30 bp segment used for the
         // estimation, the duplicate will not be discovered.
         double pdiff = pow(.461, tot);
         double pmiss = 0.2 * pdiff / (0.8 + 0.2 * pdiff); // Bayes formula.
         poff *= pmiss;
      }

      return (seedp_t) { poff, pnul };

   }
   else {
      // The estimates vary a lot. Split the read.
      // Find the cut point (use max SSE inter).
      double s1 = log(uN0[0].N0);
      double s2 = 0;
      for (int i = 1 ; i < tot ; i++) { s2 += log(uN0[i].N0); }
      int n1 = 1;
      int n2 = tot-1;
      double maxC = pow(s1,2)/n1 + pow(s2,2)/n2;
      int bkpt = 0;
      for (int i = 1 ; i < tot-1 ; i++) {
         n1++;
         n2--;
         s1 += log(uN0[i].N0);
         s2 -= log(uN0[i].N0);
         double C = pow(s1,2)/n1 + pow(s2,2)/n2;
         if (C > maxC) {
            maxC = C;
            bkpt = i;
         }
      }
      // Split (take separate medians).
      int tot1 = bkpt+1;
      int tot2 = tot - tot1;
      qsort(uN0, tot1, sizeof(uN0_t), cmpN0);
      qsort(uN0 + tot1, tot2, sizeof(uN0_t), cmpN0);

      double best_mu1 = uN0[tot1/2].u;
      int best_N01 = uN0[tot1/2].N0;
      double best_mu2 = uN0[tot1 + tot2/2].u;
      int best_N02 = uN0[tot1 + tot2/2].N0;
      
      if (best_N01 < 10*best_N02 && best_N02 < 10*best_N01) {
         // The read is fishy (possibly it has a repeat in the middle).
         // Reduce the confidence by taking the max N0 on each
         // segment instead.
         best_mu1 = uN0[tot1-1].u;
         best_N01 = uN0[tot1-1].N0;
         best_mu2 = uN0[tot -1].u;
         best_N02 = uN0[tot -1].N0;
      }

      double l1 = (tot % 2 == 0) ? 10 + 10*tot1 : 5  + 10*tot1;
      double l2 = (tot % 2 == 0) ? 10 + 10*tot2 : 15 + 10*tot2;

      double nada1;
      double nada2;
      double wrong1;
      double wrong2;

      if (skip == 0) {
         nada1 = auto_mem_seed_nullp(l1, best_mu1, best_N01);
         wrong1 = auto_mem_seed_offp(l1, best_mu1, best_N01);
         nada2 = auto_mem_seed_nullp(l2, best_mu2, best_N02);
         wrong2 = auto_mem_seed_offp(l2, best_mu2, best_N02);
      }
      else {
         nada1 = auto_skip_seed_nullp(l1, skip, best_mu1, best_N01);
         wrong1 = auto_skip_seed_offp(l1, skip, best_mu1, best_N01);
         nada2 = auto_skip_seed_nullp(l2, skip, best_mu2, best_N02);
         wrong2 = auto_skip_seed_offp(l2, skip, best_mu2, best_N02);
      }

      // Here we do need to count the probability of a purely
      // random hit only for a double "nada" (otherwise we
      // already know that there is "some" hit after seeding).
      double P = 1-exp(-(slen-GAMMA) * (idx.chr->gsize) / pow(4,GAMMA));

      double poff = nada1 * wrong2 + wrong1 * nada2 + wrong1 * wrong2;
      double pnul = nada1 * nada2 * P;

      return (seedp_t) { poff, pnul };

   }

}

double
postquality
(
         aln_t   aln,
   const char *  seq,
         index_t idx,
         int     skip
)
{

   seedp_t p = quality(aln, seq, idx, skip);
   size_t slen = strlen(seq);

   // Binomial terms (for probability of null).
   // FIXME: make sure we never get negative numbers.
   double A = lgamma(slen+1) - lgamma(slen-aln.score+1) -
      lgamma(aln.score+1) + aln.score * log(0.01) +
      (slen-aln.score) * log(0.99);
   double B = lgamma(slen-GAMMA+1) - lgamma(slen-GAMMA-aln.score+1) -
      lgamma(aln.score+1) + aln.score * log(0.75) +
      (slen-GAMMA-aln.score) * log(0.25);
   double pnul_given_score =
      1.0 / ( 1.0 + exp(A + log(1-p.nul) - B - log(p.off)) );

   return pnul_given_score + p.off >= 1.0 ? 1.0 : pnul_given_score + p.off;


}


#if 0
double
quality
(
         aln_t   aln,
   const char *  seq,
         index_t idx
)
{

   double slen = strlen(seq);
   // TODO: fix weak assert. //
   assert(slen < 250);

   uN0_t uN0[50];

   // Assume the worst (many similar duplicates).
   const double worst_case_mu = 0.02;
   const size_t worst_case_N0 = 64000;

   int tot = 0;
   for (int s = 0 ; s <= slen-30 ; s += 10, tot++) {
      uN0[tot] = estimate_uN0(aln.refseq + s, idx);
      // Case that estimation failed (e.g., because of "N").
      if (uN0[tot].u != uN0[tot].u) {
         uN0[tot] = (uN0_t) { worst_case_mu, worst_case_N0 };
      }
   }

   // Find min/max N0.
   int minN0 = 64000;
   int maxN0 = 0;
   double minu = 1.0;
   for (int i = 0 ; i < tot ; i++) {
      if (uN0[i].N0 > maxN0) maxN0 = uN0[i].N0;
      if (uN0[i].N0 < minN0) minN0 = uN0[i].N0;
      if (uN0[i].u < minu) minu = uN0[i].u;
   }

   if (maxN0 < 10 * minN0 || maxN0 < 20) {
      // All the estimates of N0 are similar. Take the median.
      qsort(uN0, tot, sizeof(uN0_t), cmpN0);

      double best_mu = uN0[tot/2].u;
      double best_N0 = uN0[tot/2].N0;

      // Probability of a purely random hit.
      double P = 1-exp(-(slen-GAMMA) * (idx.chr->gsize) / pow(4,GAMMA));
      // Probabilities of seeding errors.
      double pnul = auto_mem_seed_nullp(slen, best_mu, best_N0) * P;
      double poff = auto_mem_seed_offp(slen, best_mu, best_N0);

      if (maxN0 <= 1 && minu == 0.06) {
         // Special "high confidence" case. On every segment, the
         // estimate of N0 is 1. However, this minimum value is
         // pessimistic because it is also possible that N0 is 0,
         // in which case seeding cannot be off target. So the
         // probability that we are looking for is the probability
         // that N0 is really equal to 1, times the probability that
         // MEM seeding is off target.
         //
         // Since u = 0.06, each 10-bp window contains a difference
         // with probability 0.461. If there is a difference in the
         // central window of each 30 bp segment used for the
         // estimation, the duplicate will not be discovered.
         double pdiff = pow(.461, tot);
         double pmiss = 0.2 * pdiff / (0.8 + 0.2 * pdiff); // Bayes formula.
         poff *= pmiss;
      }

      // Binomial terms (for probability of null).
      double A = lgamma(slen+1) - lgamma(slen-aln.score+1) -
         lgamma(aln.score+1) + aln.score * log(0.01) +
         (slen-aln.score) * log(0.99);
      double B = lgamma(slen-GAMMA+1) - lgamma(slen-GAMMA-aln.score+1) -
         lgamma(aln.score+1) + aln.score * log(0.75) +
         (slen-GAMMA-aln.score) * log(0.25);
      double pnul_given_data =
         1.0 / ( 1.0 + exp(A + log(1-pnul) - B - log(poff)) );

      return pnul_given_data + poff >= 1.0 ? 1.0 : pnul_given_data + poff;

   }
   else {
      // The estimates vary a lot. Split the read.
      // Find the cut point (use max SSE inter).
      double s1 = log(uN0[0].N0);
      double s2 = 0;
      for (int i = 1 ; i < tot ; i++) { s2 += log(uN0[i].N0); }
      int n1 = 1;
      int n2 = tot-1;
      double maxC = pow(s1,2)/n1 + pow(s2,2)/n2;
      int bkpt = 0;
      for (int i = 1 ; i < tot-1 ; i++) {
         n1++;
         n2--;
         s1 += log(uN0[i].N0);
         s2 -= log(uN0[i].N0);
         double C = pow(s1,2)/n1 + pow(s2,2)/n2;
         if (C > maxC) {
            maxC = C;
            bkpt = i;
         }
      }
      // Split (take separate medians).
      int tot1 = bkpt+1;
      int tot2 = tot - tot1;
      qsort(uN0, tot1, sizeof(uN0_t), cmpN0);
      qsort(uN0 + tot1, tot2, sizeof(uN0_t), cmpN0);

      double best_mu1 = uN0[tot1/2].u;
      int best_N01 = uN0[tot1/2].N0;
      double best_mu2 = uN0[tot1 + tot2/2].u;
      int best_N02 = uN0[tot1 + tot2/2].N0;
      
      if (best_N01 < 10*best_N02 && best_N02 < 10*best_N01) {
         // The read is fishy (possibly it has a repeat in the middle).
         // Reduce the confidence by taking the max N0 on each
         // segment instead.
         best_mu1 = uN0[tot1-1].u;
         best_N01 = uN0[tot1-1].N0;
         best_mu2 = uN0[tot -1].u;
         best_N02 = uN0[tot -1].N0;
      }

      double l1 = (tot % 2 == 0) ? 10 + 10*tot1 : 5  + 10*tot1;
      double l2 = (tot % 2 == 0) ? 10 + 10*tot2 : 15 + 10*tot2;

      // Here we do not need to count the probability of a purely
      // random hit.

      double nada1 = auto_mem_seed_nullp(l1, best_mu1, best_N01);
      double wrong1 = auto_mem_seed_offp(l1, best_mu1, best_N01);

      double nada2 = auto_mem_seed_nullp(l2, best_mu2, best_N02);
      double wrong2 = auto_mem_seed_offp(l2, best_mu2, best_N02);

      return nada1 * wrong2 + wrong1 * nada2 + wrong1 * wrong2;

   }

}
#endif


void
batchmap
(
   const char * indexfname,
   const char * readsfname
)
{

   fprintf(stderr, "loading index... ");
   // Load index files.
   index_t idx = load_index(indexfname);

   // Load the genome.
   FILE * fasta = fopen(indexfname, "r");
   if (fasta == NULL) exit_cannot_open(indexfname);

   char * genome = normalize_genome(fasta, NULL);

   fprintf(stderr, "done.\n");
   FILE * inputf = fopen(readsfname, "r");
   if (inputf == NULL) exit_cannot_open(readsfname);


   size_t sz = 64;
   ssize_t rlen;
   char * seq = malloc(64);
   exit_error(seq == NULL);

   size_t counter = 0; // Used for randomizing.
   size_t maxlen = 0; // Max 'k' value for seeding probabilities.

   // Read sequence file line by line.
   while ((rlen = getline(&seq, &sz, inputf)) != -1) {
      
      // Remove newline character if present.
      if (seq[rlen-1] == '\n') seq[rlen-1] = '\0';

      // If fasta header, print sequence name.

// XXX BENCHMARK STUFF XXX //
#ifdef FASTOUT
      if (seq[0] == '>') {
         fprintf(stdout, ">%s\n", seq+1);
         continue;
      }
#else
      if (seq[0] == '>') {
         fprintf(stdout, "%s\t", seq+1);
         continue;
      }
#endif

      if (rlen > maxlen) {
         maxlen = rlen;
         // (Re)initialize library.
         sesame_set_static_params(GAMMA, rlen, PROB);
      }

      char *apos = NULL;
      double prob = 0./0.;

      int skip = 0; // Try MEM seeding first.

      for (int redo = 0 ; redo < 2 ; redo++) {

         alnstack_t *alnstack = mapread(seq, idx, genome, GAMMA, skip);
         if (!alnstack) exit(EXIT_FAILURE);

         // Did not find anything.
         if (alnstack->pos == 0) {
            free(alnstack);
            break;
         }

         // Pick a top alignment at "random".
         aln_t aln = alnstack->aln[counter++ % alnstack->pos];
         apos = chr_string(aln.refpos, idx.chr);

         // In case of ties, the quality is the
         // probability of choosing the right one.
#ifdef NOQUAL
         prob = 1.0;
#else
         prob = alnstack->pos > 1 ?
                        1.0 - 1.0 / alnstack->pos :
                        postquality(aln, seq, idx, skip);
#endif

         free(alnstack);

         // We are done if the quality is higher than 40
         // (good case) or lower than 20 (hopeless).
         if (prob < 1e-4 || prob > 1e-2) break;

         // Otherwise try skip seeds.
         skip = 8;

      }

      fprintf(stdout, "%s\t%s\t%e\n", seq,
            apos == NULL ? "NA" : apos, prob);
      free(apos);

   }

   free(seq);
   sesame_clean();

   // FIXME: Would be nice to do this, but the size is unknown.
   // FIXME: Either forget it, or store the size somewhere.
   //munmap(idx.csa);
   //munmap(idx.bwt);
   //munmap(idx.occ);
   //munmap(idx.lut);

}


int main(int argc, char ** argv) {

   // Sanity checks.
   if (argc < 2) {
      fprintf(stderr, "First argument must be \"index\" or \"mem\".\n");
      exit(EXIT_FAILURE);
   }

   if (strcmp(argv[1], "index") == 0) {
      if (argc < 3) {
         fprintf(stderr, "Specify file to index.\n");
         exit(EXIT_FAILURE);
      }
      build_index(argv[2]);
   }
   else if (strcmp(argv[1], "mem") == 0) {
      if (argc < 4) {
         fprintf(stderr, "Specify index and read file.\n");
         exit(EXIT_FAILURE);
      }
      // Argument 5 is sequencing error.
      if (argc == 5) {
         PROB = strtod(argv[4], NULL);
         if (PROB <= 0 || PROB >= 1) {
            fprintf(stderr, "Sequencing error must be in (0,1).\n");
            exit(EXIT_FAILURE);
         }
      }
      batchmap(argv[2], argv[3]);
   }
   else {
      fprintf(stderr, "First argument must be \"index\" or \"mem\".\n");
      exit(EXIT_FAILURE);
   }

}
