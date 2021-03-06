/*IMa2p 2009-2015 Jody Hey, Rasmus Nielsen, Sang Chul Choi, Vitor Sousa, Janeen Pisciotta, and Arun Sethuraman */
#undef GLOBVARS
#include "imamp.hpp"

struct histprintstructure
{
  char str[PARAMSTRLEN];
  struct plotpoint *xy;
  double yscaleadjust;
  double xscaleadjust;
  int before;
  int after;
};


static struct histprintstructure *hp;
static double scaleumean, timeumean;
static double *smthmaxvals;
static int smthmax_firstu;  /* 7/27/2012  JH  added this to fix a bug in the calculation of the geometric mean of mutation scalars for a subset of the loci */
static int numstepsrecorded;
static char histfstr[40];  
static struct plotpoint **popmigxy;

/******* LOCAL FUNCTIONS ***********/
static char *histformatdouble (double pval);
static void fillvec (void);     //fills up the marginal distribution estimates
static double multi_t_prior_func (double x, double y, double tmax, int ti,
                                  int numt);
//static void writehistogram (FILE * outfile, int numhistprint);
static void writehistogram (FILE * outfile, int numhistprint, int dosmooth);
static int getdemogscale (double scaleumeaninput);
static void prepare_splittime_and_mutation_rate_histograms (int
                                                            *numhistprint);
static void prepare_parameter_histograms (int *numhistprint);
void prepare_demographic_scale_histograms (int *numhistprint,
                                           double generationtime);
static void prepare_tmrca_histograms (int *numhistprint);
static void print_tprior_divide_histograms (FILE * outfile,
                                            int *numhistprint);
static void free_print_histogram (void);
static void init_print_histogram (int maxnumhist);

static void prepare_migration_histograms (int locusrow, int nummigdirs);

char *
histformatdouble (double pval)
{
  histfstr[0] = '\0';
  if (pval < -1e9 || pval > 1e9)
  {
    sprintf (histfstr, "%-9.0lg", pval);
    return &histfstr[0];
  }
  if (fabs (pval) < 1e-4)
  {
    sprintf (histfstr, "%-9.8lf", pval);
    if (strcmp("0.00000000",histfstr)==0)
      strcpy(histfstr,"0.0");
  }
  else if (fabs (pval) < 1e-3)
    sprintf (histfstr, "%-9.7lf", pval);
  else if (fabs (pval) < 1e-2)
    sprintf (histfstr, "%-9.6lf", pval);
  else if (fabs (pval) < 1e-1)
    sprintf (histfstr, "%-9.5lf", pval);
  else if (fabs (pval) < 1e-0)
    sprintf (histfstr, "%-9.4lf", pval);
  else if (fabs (pval) < 1e1)
    sprintf (histfstr, "%-9.3lf", pval);
  else if (fabs (pval) < 1e2)
    sprintf (histfstr, "%-9.2lf", pval);
  else if (fabs (pval) < 1e3)
    sprintf (histfstr, "%-9.1lf", pval);
  else
    sprintf (histfstr, "%-9.0lf", pval);

  return &histfstr[0];
}                               //histformatdouble


void
fillvec (void)
{
  int i, j, p;
  for (j = 0; j < GRIDSIZE; j++)
  {
    for (i = 0, p = 0; i < numpopsizeparams; i++, p++)
    {
      itheta[i].xy[j].y = margincalc ((double) itheta[i].xy[j].x, 0.0, p, 0);
    }
    for (i = 0; i < nummigrateparams; i++, p++)
    {
      if (imig[i].pr.max > MPRIORMIN)
      {
        imig[i].xy[j].y = margincalc ((double) imig[i].xy[j].x, 0.0, p, 0);
      }
    }
  }
}                               /* fillvec */

double
multi_t_prior_func (double x, double y, double tmax, int ti, int numt)
{
  double priorprob;

  priorprob =
    exp (logfact[numt] - logfact[ti] - logfact[numt - ti - 1] +
         (numt - ti - 1) * log (tmax - x) + ti * log (x) - numt * log (tmax));
  
  if (priorprob < 0)
    IM_err(IMERR_MULTITPRIOR,"beta distribution calculation for prior doesn't make sense %lf",priorprob);

  return y / priorprob;
}                               // multi_t_prior_func


/* notes on writehistogram()
Loop thru numparamsh
Main loop:
	Print summaries:
		string
		Minbin lowest x bin with nonzero y val
		Maxbin highest x bin with nonzero y val
		HiPt  x bin with highest y val
			also while identifying HiPt, calculate the sum of x, sum of y and sum of x*y
		HiSmth - x bin with highest y val on a smoothed curve
			also, if mode == 0 identify the peak and save this in uscaleml[] to be used for when mode==1
		Mean  calculated using x sum and y sum 
		95Lo  calculate lower 95% conf limit 		
		95Hi  calculate higher 95% conf limit 

		Build a sorted list of smoothed probablities use to calculate HPD90Lo and HPD90Hi
		HPD90Lo
		HPD90Hi
		
	Print Histograms
		print row of parameter strings  and 'P'
		print row of max values  'HiPt'

		print GRIDSIZE rows of x and y values 
			for y values,  multiply them by denscale[]
		print before and after values, and sum of likelihoods
Free all the pointers that were set up at the beginning 
*/

void
writehistogram (FILE * outfile, int numhistprint, int dosmooth)
{
  double *xysum;
  double *ysum;
  double *xsum;
  double *hpdlo, *hpdhi;
  char *hpdchar1,*hpdchar2;
  double *smthprobvals;
  int i, j, k, imax;
  double maxval;
  double sum, smoothsum, smoothdenom, smoothterm, tempsum;
  int cellnum, smoothcellnum = 10;
  double hpdmax, hpdmin;
  struct hlists hlist[GRIDSIZE];
  // can set to 0.95  or 0.9 or whatever
  char  hpdstr[3] = "95";  // c++ will append null terminator during init
  double hpdcutoff = 0.95;
  double hpdboundarycheck = 0.05; // use to see if probability at peak of curve is much higher than probability at boundaries
  double vminhold;

  xysum = static_cast<double *> 
          (calloc ((size_t) numhistprint, sizeof (double)));
  xsum = static_cast<double *> 
          (calloc ((size_t) numhistprint, sizeof (double)));
  ysum = static_cast<double *> 
          (calloc ((size_t) numhistprint, sizeof (double)));
  hpdlo = static_cast<double *> 
          (calloc ((size_t) numhistprint, sizeof (double)));
  hpdhi = static_cast<double *> 
          (calloc ((size_t) numhistprint, sizeof (double)));
  hpdchar1 = static_cast<char *> 
             (calloc ((size_t) numhistprint + 1, sizeof (char)));
  hpdchar2 = static_cast<char *> 
             (calloc ((size_t) numhistprint + 1, sizeof (char)));
  /* CR: 110512.1
   * Change malloc to calloc so that memory would be initialilzed.
   * before being used.
   */ 
  smthprobvals = static_cast<double *> 
                 (calloc ((size_t) numhistprint, sizeof (double)));

  FP " Summaries\n\tValue  ");
  for (j = 0; j < numhistprint; j++)
  {
    FP "\t %s", hp[j].str);
  }
  FP "\n\tMinbin ");
  for (j = 0; j < numhistprint; j++)
  {
    i = 0;
    while (i < GRIDSIZE - 1 && hp[j].yscaleadjust * hp[j].xy[i].y <= 0)
    {
      i++;
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[i].x));
  }

  FP "\n\tMaxbin");
  for (j = 0; j < numhistprint; j++)
  {
    i = GRIDSIZE - 1;
    while (i > 0 && hp[j].yscaleadjust * hp[j].xy[i].y <= 0)
    {
      i--;
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[i].x));
  }
  FP "\n\tHiPt  ");
  for (j = 0; j < numhistprint; j++)
  {
    xysum[j] = 0;
    xsum[j] = 0;
    maxval = -1;
    imax = 0;
    for (i = 0; i < GRIDSIZE; i++)
    {
      xysum[j] += hp[j].xscaleadjust * hp[j].xy[i].x * hp[j].yscaleadjust * hp[j].xy[i].y;
      xsum[j] += hp[j].xscaleadjust * hp[j].xy[i].x;
      ysum[j] += hp[j].yscaleadjust * hp[j].xy[i].y;
      if (maxval < hp[j].yscaleadjust * hp[j].xy[i].y)
      {
        maxval = hp[j].yscaleadjust * hp[j].xy[i].y;
        imax = i;
      }
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[imax].x));

  }
  if (dosmooth)
  {
    FP "\n\tHiSmth");
    for (j = 0; j < numhistprint; j++)
    {
      maxval = -1;
      imax = 0;
      i = 0;
      for (; i < GRIDSIZE; i++)
      {
        cellnum = IMIN (smoothcellnum, 2 * i);
        cellnum = IMIN (cellnum, 2 * (GRIDSIZE - 1 - i));
        k = IMAX (0, i - (cellnum / 2));
        smoothdenom = 0;
        smoothsum = 0;
        for (; k <= IMIN (GRIDSIZE - 1, i + (cellnum / 2)); k++)
        {
          smoothterm = 1.0 / (0.5 + abs (k - i));
          smoothsum += hp[j].xy[k].y * smoothterm;
          smoothdenom += smoothterm;
        }
        smoothsum /= smoothdenom;
        if (maxval < smoothsum)
        {
          maxval = smoothsum;
          smthprobvals[j] = maxval;
          imax = i;
        }
      }
      smthmaxvals[j] = hp[j].xscaleadjust * hp[j].xy[imax].x;
      FP "\t%s", histformatdouble (smthmaxvals[j]));
    }
  }
  FP "\n\tMean  ");
  for (j = 0; j < numhistprint; j++)
  {
    FP "\t%s", histformatdouble (xysum[j] / ysum[j]));
  }
  FP "\n\t95%%Lo  ");
  for (j = 0; j < numhistprint; j++)
  {
    i = 0;
    sum = 0;
    while ((sum + hp[j].yscaleadjust * hp[j].xy[i].y) / ysum[j] <= 0.025
           && i < GRIDSIZE)
    {
      sum += hp[j].yscaleadjust * hp[j].xy[i].y;
      i++;
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[i].x));
  }
  FP "\n\t95%%Hi  ");
  for (j = 0; j < numhistprint; j++)
  {
    i = GRIDSIZE - 1;
    sum = 0;
    while ((sum + hp[j].yscaleadjust * hp[j].xy[i].y) / ysum[j] <=
           0.025 && i > 0)
    {
      sum += hp[j].yscaleadjust * hp[j].xy[i].y;
      i--;
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[i].x));
  }
  /* print out Highest Posterior Density intervals  - first, smooth the curve 
  and make a copy of the curve in hlist */
  smoothcellnum = 30;
  for (j = 0; j < numhistprint; j++)
  {
    hpdchar1[j] = ' ';
    hpdchar2[j] = ' ';
    maxval = -1;
    i = 0;
    tempsum = 0;
    for (; i < GRIDSIZE; i++)
    {
      cellnum = IMIN (smoothcellnum, 2 * i);
      cellnum = IMIN (cellnum, 2 * (GRIDSIZE - 1 - i));
      k = IMAX (0, i - (cellnum / 2));
      smoothdenom = 0;
      smoothsum = 0;
      for (; k <= IMIN (GRIDSIZE - 1, i + (cellnum / 2)); k++)
      {
        smoothterm = 1.0 / (0.5 + abs (k - i));
        smoothsum += hp[j].xy[k].y * smoothterm;
        smoothdenom += smoothterm;
      }
      hlist[i].v = hp[j].xscaleadjust * hp[j].xy[i].x;
      smoothsum /= smoothdenom;
      tempsum += smoothsum;
      hlist[i].p = smoothsum;
    }
    vminhold = hlist[0].v;
/* short hlist by probability from low to high  
  move up the list from the bottom and accumulate a sum
  until the sum = hpdcutoff of the total. 
  while moving up the list, find the values associated with the 
  probability that pushes the sum over hpdcutoff 
*/
    shellhist (&(hlist[0]), GRIDSIZE);
    sum = 0;
    hpdmax = -1;
    hpdmin = 1e10;
    i = GRIDSIZE - 1;
    sum = hlist[i].p;
    while (i >= 0 && sum <= (hpdcutoff * tempsum))
    {
      if (i > 0 && hlist[i].p < hlist[i - 1].p)
        IM_err(IMERR_HPD95,"problem calculating HPD interval",i,hlist[i].p, hlist[i-1].p);
      if (hlist[i].v > hpdmax)
      {
        hpdmax = hlist[i].v;
      }
      if (hlist[i].v < hpdmin)
      {
        hpdmin = hlist[i].v;
      }
      i--;
      sum += hlist[i].p;
    }
// set the lower bound to zero if the found lower bound is at the minimum possible value of the hpd histogram
// this will break if we start using priors with nonzero lower bounds 
    if (hpdmin <= vminhold)
      hpdlo[j] = 0.0;
    else
      hpdlo[j] = hpdmin;
    hpdhi[j] = hpdmax;
    while (i > 0 && (hlist[i].v < hpdmin || hlist[i].v > hpdmax))
      i--;
    if (i > 0)
    {
      hpdchar2[j] = '?';
    }

    if ((smthprobvals[j] * hpdboundarycheck < hp[j].xy[0].y) && (smthprobvals[j] * hpdboundarycheck < hp[j].xy[GRIDSIZE-1].y))
    {
      hpdchar1[j] = '#';
    }
  }
  FP "\n\tHPD%sLo",hpdstr);
  for (j = 0; j < numhistprint; j++)
  {
    FP "\t%s%c%c", histformatdouble (hpdlo[j]), hpdchar1[j], hpdchar2[j]);
  }
  FP "\n\tHPD%sHi",hpdstr);
  for (j = 0; j < numhistprint; j++)
  {
    FP "\t%s%c%c", histformatdouble (hpdhi[j]), hpdchar1[j],hpdchar2[j]);
  }
  FP "\n");
  FP "\n");
  FP "\tParameter");
  for (j = 0; j < numhistprint; j++)
    FP "\t%s\tP", hp[j].str);
  FP "\n\tHiPt");
  for (j = 0; j < numhistprint; j++)
  {
    maxval = -1;
    imax = 0;
    for (i = 0; i < GRIDSIZE; i++)
    {
      if (maxval < hp[j].yscaleadjust * hp[j].xy[i].y)
      {
        maxval = hp[j].yscaleadjust * hp[j].xy[i].y;
        imax = i;
      }
    }
    FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[imax].x));
    FP "\t%s", histformatdouble (hp[j].yscaleadjust * hp[j].xy[imax].y));
  }
  FP "\n\n");
  for (i = 0; i < GRIDSIZE; i++)
  {
    FP "\t%4d", i);
    for (j = 0; j < numhistprint; j++)
    {
      FP "\t%s", histformatdouble (hp[j].xscaleadjust * hp[j].xy[i].x));
      FP "\t%s", histformatdouble (hp[j].yscaleadjust * hp[j].xy[i].y));
    }
    FP "\n");
  }
  FP " SumP\t");
  for (j = 0; j < numhistprint; j++)
    FP "\t\t%s", histformatdouble (ysum[j]));
  FP "\n");
  FP " Before\t");
  for (j = 0; j < numhistprint; j++)
    FP "\t\t%s", histformatdouble (hp[j].before * hp[j].yscaleadjust));

  FP "\n");
  FP " After\t");
  for (j = 0; j < numhistprint; j++)
    FP "\t\t%s", histformatdouble (hp[j].after * hp[j].yscaleadjust));
  FP "\n");
  XFREE (xysum);
  XFREE (xsum);
  XFREE (ysum);
  XFREE (hpdlo);
  XFREE (hpdhi);
  XFREE (hpdchar1);
  XFREE (hpdchar2);
  XFREE (smthprobvals);
  return;
}                               /* writehistogram */

int getdemogscale (double scaleumeaninput)
{
  int i, cui, ui, li;


  if (runoptions[LOADRUN])
  {
    if (scaleumeaninput <= 0)
    {
      return 0;
    }
    else
    {
      scaleumean = scaleumeaninput;
      timeumean = 0;
      for (li = 0, cui = 0; li < nloci; li++)
        for (i = 0; i < L[li].nlinked; i++)
          if (L[li].uperyear_vals[i] > 0)
          {
            timeumean += log (L[li].uperyear_vals[i]);
            cui++;
          }
      assert (cui > 0);
      timeumean = exp (timeumean / cui);
    }
  }
  else
  {
    scaleumean = 0;
    timeumean = 0; 
    for (li = 0, ui = smthmax_firstu , cui = 0; li < nloci; li++) /* 7/27/2012  JH  replaced ui = 0 with ui = smthmax_firstu. This to fix a bug in the calculation of the geometric mean of mutation scalars for a subset of the loci */
      for (i = 0; i < L[li].nlinked; i++, ui++)
      {
        if (L[li].uperyear_vals[i] > 0)
        {
          timeumean += log (L[li].uperyear_vals[i]);
          if (nurates > 1)
            scaleumean += log (smthmaxvals[ui]);
          cui++;
        }
      }
    timeumean = exp (timeumean / cui);
    if (scaleumeaninput > 0)
    {
      scaleumean = scaleumeaninput;
    }
    else
    {
      assert (cui);
      scaleumean = exp (scaleumean / cui);
    }
  }
  return cui;
}                               //getdemogscale 


void prepare_splittime_and_mutation_rate_histograms (int *numhistprint)
{
  int i, ui, li;
  for (i = 0, *numhistprint = 0; i < numsplittimes; i++, (*numhistprint)++)
  {
    strcpy (hp[*numhistprint].str, T[i].str);
    hp[*numhistprint].xy = T[i].v->xy;
    /* use full range,  assumming minimum t is zero,  for this purpose */
    hp[*numhistprint].yscaleadjust =  (GRIDSIZE / (T[i].pr.max)) / numstepsrecorded;
    hp[*numhistprint].xscaleadjust = 1;
    hp[*numhistprint].before = T[i].v->beforemin;
    hp[*numhistprint].after = T[i].v->aftermax;
  }
  if (runoptions[LOADRUN] == 0 && (nurates > 1))
  {
    smthmax_firstu = *numhistprint;  /* 7/27/2012  JH  added this to fix a bug in the calculation of the geometric mean of mutation scalars for a subset of the loci */
    for (li = 0; li < nloci; li++)
      for (ui = 0; ui < L[li].nlinked; ui++)
      {
        assert (ui < L[li].nlinked);
        strcpy (hp[*numhistprint].str, L[li].u_rec[ui].str);
        hp[*numhistprint].xy = L[li].u_rec[ui].v->xy;
        hp[*numhistprint].yscaleadjust = (GRIDSIZE / (exp (L[li].u_rec[ui].pr.max) - exp (L[li].u_rec[ui].pr.min))) / numstepsrecorded;
        hp[*numhistprint].before = L[li].u_rec[ui].v->beforemin;
        hp[*numhistprint].after = L[li].u_rec[ui].v->aftermax;
        hp[*numhistprint].xscaleadjust = 1;
        (*numhistprint)++;
      }
    for (li = 0; li < nloci; li++)
      for (ui = 0; ui < L[li].nlinked; ui++)
        if (L[li].umodel[0] == HKY)
        {
          assert (ui < L[li].nlinked);
          strcpy (hp[*numhistprint].str, L[li].kappa_rec->str);
          hp[*numhistprint].xy = L[li].kappa_rec->v->xy;
          hp[*numhistprint].yscaleadjust = (GRIDSIZE / (L[li].kappa_rec->pr.max - L[li].kappa_rec->pr.min)) / numstepsrecorded;
          hp[*numhistprint].before = L[li].kappa_rec->v->beforemin;
          hp[*numhistprint].after = L[li].kappa_rec->v->aftermax;
          hp[*numhistprint].xscaleadjust = 1;
          (*numhistprint)++;
        }
  }
}                               // prepare_splittime_and_mutation_rate_histograms(int *numhistprint)


void prepare_parameter_histograms (int *numhistprint)
{
  int i;
  fillvec ();
  *numhistprint = 0;
  for (i = 0; i < numpopsizeparams; i++)
  {
    hp[*numhistprint].xy = itheta[i].xy;
    strcpy (hp[*numhistprint].str, itheta[i].str);
    hp[*numhistprint].yscaleadjust = 1;
    hp[*numhistprint].xscaleadjust = 1;
    hp[*numhistprint].before = 0;
    hp[*numhistprint].after = 0;
    (*numhistprint)++;
  }
  for (i = 0; i < nummigrateparams; i++)
    if (imig[i].pr.max > MPRIORMIN)
    {
      hp[*numhistprint].xy = imig[i].xy;
      strcpy (hp[*numhistprint].str, imig[i].str);
      hp[*numhistprint].yscaleadjust = 1;
      hp[*numhistprint].xscaleadjust = 1;
      hp[*numhistprint].before = 0;
      hp[*numhistprint].after = 0;
      (*numhistprint)++;
    }
}                               //void prepare_parameter_histograms(int *numhistprint);

void prepare_demographic_scale_histograms (int *numhistprint,
                                           double generationtime)
{
  int i;
  for (i = 0, *numhistprint = 0; i < numsplittimes; i++, (*numhistprint)++)
  {
    strcpy (hp[*numhistprint].str, T[i].str);
 
    hp[*numhistprint].xy = T[i].v->xy;
    /* use full range,  assumming minimum t is zero,  for this purpose */
    hp[*numhistprint].yscaleadjust = (GRIDSIZE / (T[i].pr.max)) / numstepsrecorded;
    hp[*numhistprint].xscaleadjust = scaleumean / timeumean;
    hp[*numhistprint].before = T[i].v->beforemin;
    hp[*numhistprint].after = T[i].v->aftermax;
  }
  
  for (i = 0; i < numpopsizeparams; i++)
  {
    hp[*numhistprint].xy = itheta[i].xy;
    strcpy (hp[*numhistprint].str, itheta[i].str);
    hp[*numhistprint].yscaleadjust = 1;
    hp[*numhistprint].xscaleadjust =
      scaleumean / (4 * timeumean * generationtime);
    hp[*numhistprint].before = 0;
    hp[*numhistprint].after = 0;
    (*numhistprint)++;
  }
}                               //void prepare_demographic_scale_histograms(int *numhistprint);

void prepare_tmrca_histograms (int *numhistprint)
{
  int li;
  for (*numhistprint = 0, li = 0; li < nloci; li++)
  {
    hp[*numhistprint].xy = L[li].g_rec->v->xy;
    strcpy (hp[*numhistprint].str, L[li].g_rec->v->str);
    hp[*numhistprint].yscaleadjust = 1 / (double) numstepsrecorded;
    hp[*numhistprint].xscaleadjust = 1;
    hp[*numhistprint].before = 0;
    hp[*numhistprint].after = 0;
    (*numhistprint)++;
  }

}                               //void prepare_tmrca_histograms(int *numhistprint);

void print_tprior_divide_histograms (FILE * outfile, int *numhistprint)
{
  struct plotpoint **t_prior_divide;
  int i, j;

  t_prior_divide = static_cast<plotpoint **> (malloc (numsplittimes * sizeof (struct plotpoint *)));
  for (i = 0; i < numsplittimes; i++)
  {
    t_prior_divide[i] = static_cast<plotpoint *> (malloc (GRIDSIZE * sizeof (struct plotpoint)));
    for (j = 0; j < GRIDSIZE; j++)
    {
      t_prior_divide[i][j].x = T[i].v->xy[j].x;
      t_prior_divide[i][j].y =
        multi_t_prior_func (T[i].v->xy[j].x, T[i].v->xy[j].y,
                            T[i].pr.max, i, numsplittimes);
    }

  }
  for (i = 0, *numhistprint = 0; i < numsplittimes; i++, (*numhistprint)++)
  {
    strcpy (hp[*numhistprint].str, T[i].str);
    hp[*numhistprint].xy = t_prior_divide[i];
    /* use full range,  assumming minimum t is zero,  for this purpose */
    hp[*numhistprint].yscaleadjust = (GRIDSIZE / (T[i].pr.max)) / numstepsrecorded;
    hp[*numhistprint].xscaleadjust = scaleumean / timeumean;;
    hp[*numhistprint].before = T[i].v->beforemin;
    hp[*numhistprint].after = T[i].v->aftermax;
  }
  writehistogram (outfile, *numhistprint,1);
  orig2d_free2D ((void **) t_prior_divide, numsplittimes);
}                               //void prepare_tprior_divide_histograms(int *numhistprint);



void print_populationmigrationrate_histograms (FILE * outfile,
                                               int *numhistprint,
                                               int prob_or_like)
{
  int i, j, k, hpi, mpop, thetai, mi, found;
  char tempstr[PARAMSTRLEN];
  double pmmax, tempy, tempx, maxxfind;
  popmigxy = (struct plotpoint **) malloc((size_t)(*numhistprint * sizeof (struct plotpoint *)));
  for (i = 0; i <*numhistprint; i++)
    popmigxy[i] = (struct plotpoint *) malloc ((size_t)  (GRIDSIZE * sizeof (struct plotpoint)));

  hpi = 0;
  if (modeloptions[PARAMETERSBYPERIOD])
  {
    for (k = 0; k < lastperiodnumber; k++)
    {
      for (i = 0; i < npops - k; i++)
      {
        mpop = C[0]->plist[k][i];

        thetai = 0;
        found = 0;
        while (!found && thetai < numpopsizeparams)
        {
          found = (k == atoi (&itheta[thetai].str[1])
                   && mpop == atoi (&itheta[thetai].str[3]));
          if (!found)
            thetai++;
        }
        assert (thetai < numpopsizeparams);
        for (mi = 0; mi < nummigrateparams; mi++)
        {
          found = 0;
          if (modeloptions[SINGLEMIGRATIONBOTHDIRECTIONS])
          {
            found = (k == atoi (&imig[mi].str[1])
                     && (mpop == atoi (&imig[mi].str[3])
                         || mpop == atoi (&imig[mi].str[6])));
          }
          else
          {
            found = (k == atoi (&imig[mi].str[1])
                     && mpop == atoi (&imig[mi].str[3]));
          }
          if (found)
          {
            sprintf (tempstr, "%d,2N%d", k, mpop);
            if (modeloptions[SINGLEMIGRATIONBOTHDIRECTIONS])
            {
              strcat (tempstr, "m");
              strcat (tempstr, &imig[mi].str[3]);
            }
            else
            {
              strcat (tempstr, imig[mi].str);
            }
            strcpy (hp[hpi].str, tempstr);
            if (modeloptions[EXPOMIGRATIONPRIOR])
            {
              pmmax = EXPOMIGPLOTSCALE * imig[mi].pr.mean;
            }
            else
            {
              pmmax = itheta[thetai].pr.max * imig[mi].pr.max / 2.0;
            }
            j = GRIDSIZE-1;
            maxxfind = 0;
            while (maxxfind==0 && j >= 0)
            {
              tempx = (j + 0.5) * pmmax / GRIDSIZE;
              if (modeloptions[EXPOMIGRATIONPRIOR])
                tempy = calc_pop_expomig (thetai, mi,tempx , prob_or_like);
              else
                tempy = calc_popmig (thetai, mi,tempx , prob_or_like);
              if (tempy > 1e-9)
                maxxfind = tempx;
              else 
                j--;
            }
            if (j < 0)
              maxxfind = pmmax;
            for (j = 0; j < GRIDSIZE; j++)
            {
              popmigxy[hpi][j].x = (j + 0.5) * maxxfind / GRIDSIZE;
              if (modeloptions[EXPOMIGRATIONPRIOR])
              {
                popmigxy[hpi][j].y = 1;
              }
              else
              {
                popmigxy[hpi][j].y = calc_popmig (thetai, mi, popmigxy[hpi][j].x, prob_or_like);
              }
            }
            hp[hpi].xy = popmigxy[hpi];
            hp[hpi].xscaleadjust = hp[hpi].yscaleadjust = 1;
            hp[hpi].before = 0;
            hp[hpi].after = 0;
            hpi++;
          }
        }
      }
    }
  }
  else
  {
    for (i = 0; i < numtreepops - 1; i++)
    {
      thetai = i;
      for (mi = 0; mi < nummigrateparams; mi++)
      {
        if (modeloptions[SINGLEMIGRATIONBOTHDIRECTIONS])
        {
          found = ((thetai == atoi (&imig[mi].str[1])) || (thetai == atoi (&imig[mi].str[4])));
        }
        else
        {
          found = thetai == atoi (&imig[mi].str[1]);
        }
        if (found)
        {
          sprintf (tempstr, "2N%d", thetai);
          strcat (tempstr, imig[mi].str);
          strcpy (hp[hpi].str, tempstr);
          if (modeloptions[EXPOMIGRATIONPRIOR])
          {
            pmmax = EXPOMIGPLOTSCALE * imig[mi].pr.mean;
          }
          else
          {
            pmmax = itheta[thetai].pr.max * imig[mi].pr.max / 2.0;
          }
          j = GRIDSIZE-1;
          maxxfind = 0;
          while (maxxfind==0 && j >= 0)
          {
            tempx = (j + 0.5) * pmmax / GRIDSIZE;
            if (modeloptions[EXPOMIGRATIONPRIOR])
              tempy = calc_pop_expomig (thetai, mi,tempx , prob_or_like);
            else
              tempy = calc_popmig (thetai, mi,tempx , prob_or_like);
            if (tempy > 1e-9)
              maxxfind = tempx;
            else 
              j--;
          }
          if (j < 0)
            maxxfind = pmmax;
          for (j = 0; j < GRIDSIZE; j++)
          {
            popmigxy[hpi][j].x = (j + 0.5) * maxxfind / GRIDSIZE;
            if (modeloptions[EXPOMIGRATIONPRIOR])
              popmigxy[hpi][j].y = calc_pop_expomig (thetai, mi, popmigxy[hpi][j].x, prob_or_like);
            else
              popmigxy[hpi][j].y = calc_popmig (thetai, mi, popmigxy[hpi][j].x, prob_or_like);
          }
          hp[hpi].xy = popmigxy[hpi];
          hp[hpi].xscaleadjust = hp[hpi].yscaleadjust = 1;
          hp[hpi].before = 0;
          hp[hpi].after = 0;
          hpi++;
        }
      }
    }
  }
  writehistogram (outfile, hpi,0);
  orig2d_free2D ((void **) popmigxy, *numhistprint);
}                               //void print_populationmigrationrate_histograms


void prepare_migration_histograms (int locusrow, int nummigdirs)
{
  int i;
/* 8/26/2011 */
  for (i = 0; i < nummigdirs; i++)
  {
    hp[i].xy = migration_counts[locusrow][i].xy;
    strcpy (hp[i].str, migration_counts[locusrow][i].str);
    hp[i].yscaleadjust = 1 / (double) numstepsrecorded;
    hp[i].xscaleadjust = 1;
    hp[i].before = 0;
    hp[i].after = 0;
  }
/*
  for (i = 0; i < 2 * nummigdirs; i++)
  {
    hp[i].xy = migration_counts_times[locusrow][i].xy;
    strcpy (hp[i].str, migration_counts_times[locusrow][i].str);
    hp[i].yscaleadjust = 1 / (double) numstepsrecorded;
    hp[i].xscaleadjust = 1;
    hp[i].before = 0;
    hp[i].after = 0;
  } */
}                               // prepare migration histograms

void free_print_histogram (void)
{
  XFREE (hp);
  XFREE (smthmaxvals);
}                               // free_print_histogram 

void init_print_histogram (int maxnumhist)
{
  hp = (struct histprintstructure *) malloc (maxnumhist * sizeof (struct histprintstructure));
  smthmaxvals = static_cast<double *> (malloc (maxnumhist * sizeof (double)));
}                               // init_print_histogram 

/***** GLOBAL FUNCTIONS **********/

/* to print one or more histograms:
-----------------------------------

histograms are printing using 
static struct histprintstructure *hp;  
The details of stuct histprintstructure are given at the top of this file. 

There are two main steps to printing a table with multiple histograms:
1)write a function to prepare *hp ( pointer to struct histprintstructure) 
hp already exists,  but where it points to needs to be set. 
This function should also set the value of numhistprint, the number of histograms to print
	e.g. prepare_myhistogram(&humhistprint)
2) make a call to writehistogram(), which should print whatever *hp is pointing at

For example:
write a function  prepare_myhistogram(&humhistprint)
Add the following function calls to printhistograms():
  prepare_myhistogram(&humhistprint)
  writehistogram (outfile, numhistprint); 
It is also helpful to precede these by some FP statements that explain the histograms. 

*/

void printhistograms (FILE * outfile, long int recordstep,
                      double generationtime, double scaleumeaninput)
{
  int numhistprint = 0, uratecount;
  int li, predictmcmchist = 0, predictparamhist = 0, numhist; 
  int numhistsets, histsetcount;

  // number passed to init_print_histograms  just needs to be large enough to hold as many histograms as might be printed

  for (li = 0; li < nloci; li++)
    predictmcmchist += L[li].nlinked + (L[li].umodel[0] == HKY);
  predictparamhist = numpopsizeparams + 2 * nummigrateparams;
  numhist = numsplittimes + IMAX(predictmcmchist,predictparamhist);
  init_print_histogram (numhist);
  numstepsrecorded = recordstep;

  FP "HISTOGRAMS\n");
  FP "==========\n");
  FP "  Each histogram is given as %d pairs of values (i.e. two columns side by side).\n", GRIDSIZE);
  FP "  In each case the left column is the value of the parameter or term (i.e. x value)\n");
  FP "  and the right column is the estimated posterior probability (i.e. y value).\n");
  FP "  HPD (Highest Posterior Density) intervals are estimated, and may be incorrect: \n");
  FP "  Possible HPD footnotes: \n");
  FP "       '?' HPD interval may be incorrect due to multiple peaks\n");
  FP "       '#' HPD may not be useful - posterior density does not reach low levels near either the upper or the lower limit of the prior\n");

  numhistsets = 2;
  numhistsets += (outputoptions[POPMIGPARAMHIST] && nummigrateparams > 0);
  numhistsets += (outputoptions[PRINTTMRCA]);
  numhistsets += (outputoptions[THISTDIVIDEBYPRIOR] && numsplittimes > 1);
  FP"\nNUMBER OF GROUPS OF HISTOGRAM TABLES : %d\n\n",numhistsets);

  histsetcount = 1;
  prepare_splittime_and_mutation_rate_histograms (&numhistprint);
  if (numhistprint)
  {
    FP "HISTOGRAM GROUP %d: MARGINAL DISTRIBUTION VALUES AND HISTOGRAMS OF PARAMETERS IN MCMC\n", histsetcount);
    FP "----------------------------------------------------------------------------------\n");
    FP "    curve height is an estimate of marginal posterior probability\n");
    if (runoptions[LOADRUN])
      FP "  IMa LOAD TREES MODE  - splittime values loaded from *.ti file, mutation rate scalar histograms are not available \n");
    writehistogram (outfile, numhistprint,1);
    //  calculate the average mutation rate scalar 
    if (outputoptions[PRINTDEMOGHIST])  // need to set the scalars needed for demographic histograms using info contained in smthmaxvals[], which was set in in last call to writehistogram
    {
      uratecount = getdemogscale (scaleumeaninput);
      numhistsets += ((runoptions[LOADRUN] && (scaleumeaninput <= 0)) || uratecount == 0);
    }
    numhistprint = 0;
    histsetcount++;
  }

  prepare_parameter_histograms (&numhistprint);
  if (numhistprint)
  {
    FP "\n\nHISTOGRAM GROUP %d: MARGINAL DISTRIBUTION VALUES AND HISTOGRAMS OF POPULATION SIZE AND MIGRATION PARAMETERS\n", histsetcount);
    FP "--------------------------------------------------------------------------------------------------------\n");
    FP "       curve height is an estimate of marginal posterior probability\n");
    writehistogram (outfile, numhistprint,0);
  }
  numhistprint = 0;

  if (outputoptions[PRINTDEMOGHIST])
  {
    if ((runoptions[LOADRUN] && (scaleumeaninput <= 0)) || uratecount == 0)
    {
      FP "\n\nPROBLEM CALCULATING HISTOGRAMS ON DEMOGRAPHIC SCALES\n");
      if (runoptions[LOADRUN] && (scaleumeaninput <= 0))
        FP " If run in LOADMODE,  user must provide the geometric mean mutation scalar estimate \n");
      if (uratecount == 0)
        FP " Mutation rates not provided in input file - at least one locus must have a mutation rate provided \n");
    }
    else
    {
      numhistprint = 0;
      prepare_demographic_scale_histograms (&numhistprint, generationtime);
      if (numhistprint)
      {
        histsetcount++;
        FP "\n\nHISTOGRAM GROUP %d: MARGINAL DISTRIBUTION VALUES IN DEMOGRAPHIC UNITS\n", histsetcount);
        FP "--------------------------------------------------------------------\n");
        FP "\tCalculations use mutation rates (in years) and generation time (in years) input at runtime\n");
        FP "\t  - note, curve height has not been adjusted with the scale change. Integration does not equal 1 \n");
        FP "\t  - # of loci with mutation rates in input file : %d \n",uratecount);
        FP "\tRescaled Population Size Parameter Units: individuals \n");
        FP "\tRescaled Time Parameter Units: years\n");
        FP "\tGeneration time in years specified on command line at runtime: %lf \n", generationtime);
        FP "\tGeometric mean of mutation rates per year (based on rates specified in input file): %le\n", timeumean);
        if (runoptions[LOADRUN])
          FP "\tGeometric mean of ML estimates of relevant mutation rate scalars given on commandline at runtime: %le\n", scaleumeaninput);
        else
        {
          if (scaleumeaninput > 0)
            FP "\tGeometric mean of ML estimates of relevant mutation rate scalars given on commandline at runtime: %le\n", scaleumeaninput);
          else 
            FP "\tGeometric mean of ML estimates of relevant mutation rate scalars calculated from scalar histograms: %le\n", scaleumean);
        }
        writehistogram (outfile, numhistprint,1);
      }
    }
  }
  else
  {
    scaleumean = 1;
    timeumean = 1;
  }
  if (outputoptions[POPMIGPARAMHIST] && nummigrateparams > 0)
  {
    if (modeloptions[SINGLEMIGRATIONBOTHDIRECTIONS])
      numhistprint = 2 * nummigrateparams;
    else
      numhistprint = nummigrateparams;
    if (numhistprint)
    {
      histsetcount++;
      FP "\n\nHISTOGRAM GROUP %d: POPULATION MIGRATION (2NM) POSTERIOR PROBABILITY HISTOGRAMS\n", histsetcount);
      FP "-----------------------------------------------------------------------------\n");
      FP "     curve height is an estimate of the posterior probability\n");
      FP "      each term is the product of a population parameter (e.g. q0) and a migration rate (e.g.m0>1) \n");
      FP "      migration rates are in the coalescent (backwards in times), so that a population migration rate of \n");
      FP "        q1m0>1  is the population rate (forward in time) at which population 1 receives migrants from population 0\n");
      print_populationmigrationrate_histograms (outfile, &numhistprint, 0);       // writehistogram() called from within this because of memory allocation within 
    }
    /*  not sure if it is useful to get likelihoods  for 2NM values,  or what they even mean,  drop this  4/24/09
    FP "\n\nPOPULATION MIGRATION (2NM) RELATIVE LIKELIHOOD HISTOGRAMS\n");
    FP "------------------------------------------------------------\n");
    FP "     curve height is an estimate of the relative likelihood\n");
    FP "      each histogram is the posterior probability (see histograms for population migration terms) divided by the prior probability\n");
    FP "      each term is the product of a population parameter (e.g. q0) and a migration rate (e.g.m0>1) \n");
    FP "      migration rates are in the coalescent (backwards in times), so that a population migration rate of \n");
    FP "        q1m0>1  is the population rate (forward in time) at which population 1 receives migrants from population 0\n");
    
    print_populationmigrationrate_histograms (outfile, &numhistprint, 1);       // writehistogram() called from within this because of memory allocation within 
    */
  }
  if (outputoptions[PRINTTMRCA])
  {
    numhistprint = 0;
    prepare_tmrca_histograms (&numhistprint);
    if (numhistprint)
    {
      histsetcount++;
      FP "\n\nHISTOGRAM GROUP %d: MARGINAL DISTRIBUTIONS OF TMRCA VALUES\n", histsetcount);
      FP "--------------------------------------------------------\n");
      writehistogram (outfile, numhistprint,1);
    }
  }
  if (outputoptions[THISTDIVIDEBYPRIOR] && numsplittimes > 1)
  {
    histsetcount++;
    FP "\n\nHISTOGRAM GROUP %d: POPULATION SPLITTING TIME LIKELIHOODS (SPLITTIME TIME HISTOGRAMS DIVIDED BY PRIOR DISTRIBUTIONS)\n",histsetcount);
    FP "----------------------------------------------------------------------------------------------------------------\n");
    FP "     curve height is an estimate of the relative marginal likelihood\n");
    numhistprint = 0;
    print_tprior_divide_histograms (outfile, &numhistprint);    // writehistogram() called from within this because of memory allocation within 
  }
  free_print_histogram ();
}                               // printhistograms

/* plots of counts and times of migration events are handled a little differently than 
  other histograms.  They still are done by a call to writehistogram(), but the printing
  is done to a different file,  so that's why this function was created */
void printmigrationhistograms (FILE * outfile, long int recordstep)
{
  int i, nummigdirs;
  nummigdirs = 2*(npops-1)*(npops-1);
  //init_print_histogram (2 * nummigrateparams);
  init_print_histogram (2 * nummigdirs);
  numstepsrecorded = recordstep;

  FP "================================================================\n");
  FP "MIGRATION COUNT DISTRIBUTIONS:  BY LOCUS AND MIGRATION PARAMETER\n");
  FP "================================================================\n\n");
  FP "NOTE: As of 4/1/2011,  IMa2 does not record the distribution of migration times.\n\n");

  FP "    All migration events are recorded in the coalescent direction (i.e. backwards in time)\n");
  FP "    If there are multiple loci, the first set of histograms is for sums across loci \n");
/*
  if (nloci > 1)
  {
    FP "    Each set of histograms corresponds to migration counts recorded for one locus \n");
    FP "    All migration events are recorded in the coalescent direction (i.e. backwards in time)\n");
    FP "    If there are multiple loci, the first set of histograms is for sums across loci \n");
  } */
/*  FP "     Curves labeled by source and target population and " "_t"
    "  give the distribution of the time of migration events\n");
  FP "         in the sampled genealogies  \n");
  FP "         in the sampled genealogies  \n\n");
  FP "NOTE: As of 4/1/2011,  IMa2 no longer records the distribution of migration times.\n\n"); */

  /*FP "     Curves labeled by by source and target population and " "_#"
    " give the distribution of the number of migration \n");
  FP "         events in the sampled genealogies\n"); */

  FP "===========================================================================================================\n\n");
  for (i = 0; i < nloci + (nloci > 1); i++)
  {
    prepare_migration_histograms (i, nummigdirs);
    FP "===================\n");
    FP " LOCUS: ");
    if (nloci > 1 && i == 0)
    {
      FP " SUM ACROSS ALL LOCI\n");
    }
    else
    {
      if (nloci > 1)
        FP " %s\n", L[i - 1].name);
      else
        FP " %s\n", L[i].name);
    }
    FP "===================\n");
    /* 8/26/2011 */
    writehistogram (outfile, nummigdirs,1);
    //writehistogram (outfile, 2 * nummigdirs,1);
  }
  free_print_histogram ();
}                               // printmigrationhistograms
