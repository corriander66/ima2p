/*IMa2p 2009-2015 Jody Hey, Rasmus Nielsen, Sang Chul Choi, Vitor Sousa, Janeen Pisciotta, and Arun Sethuraman */
#undef GLOBVARS
#include "imamp.hpp"

/*********** LOCAL STUFF **********/
//static unsigned long swapcount[MAXCHAINS][MAXCHAINS];
static double swapweight (int ci, int cj);
static double calcpartialswapweight(int c);
static double swapweight_bwprocesses(double sumi, double sumj, double betai, double betaj);


double
swapweight (int ci, int cj)
{

/* 5/19/2011 JH adding thermodynamic integration */

  int li;
  double sumi = 0, sumj = 0, w;
  for (li = 0; li < nloci; li++)
  {
    sumi += C[ci]->G[li].pdg;
    sumj += C[cj]->G[li].pdg;
  }
  if (calcoptions[CALCMARGINALLIKELIHOOD])
    w = exp ((beta[ci] - beta[cj]) * (sumj - sumi));
  else
  {
  sumi += C[ci]->allpcalc.probg;
  sumj += C[cj]->allpcalc.probg;
  w = exp ((beta[ci] - beta[cj]) * (sumj - sumi));
  }
  return (w);
}

///AS: Adding a function to only calculate the sums for a particular chain
//this would then be shared with the swapper process/chain
double
calcpartialswapweight (int c)
{
	int l;
	double sum = 0, w;
	for (l = 0; l < nloci; l++)
	{
	sum += C[c]->G[l].pdg;
	}
	if (!calcoptions[CALCMARGINALLIKELIHOOD]) {
		sum += C[c]->allpcalc.probg;
	}
	return (sum);
}
/* End of function calcpartialswapweight() */

///AS: Adding a function to calculate the swapweight given sums and betas passed
//from the swapping chains on different processes.

double
swapweight_bwprocesses(double sumi, double sumj, double betai, double betaj)
{
	double w = exp ((betai - betaj) * (sumj - sumi));
	return(w);
}
/* End of function swapweight_bwprocesses() */





/************ GLOBAL FUNCTIONS ******************/

void
setheat (double hval1, double hval2, int heatmode, int currentid)
{
  int ci;
  int x = 0;
  double h = 0.0;
   /* 5/19/2011 JH adding thermodynamic integration */
//AS: Changing how i maintain count of swaps as on 6/13/2014
//allbetas is an array that's saved on each processor
//therefore, every time i swap, i check beta that is received/being sent with allbetas, then add 1 
//to the corresponding temperature, instead of the chain id.
//	if (currentid == 0) {
		allbetas = static_cast<double *> (malloc ((numchains * numprocesses) * sizeof (double)));
		///FILL up all betas - this would be faster than actually having to send it from other processes, I think
		for (int i = 0; i < numprocesses * numchains; i++) {
				switch (heatmode)
				{
				case HLINEAR:
					allbetas[x] = 1.0 / (1.0 + hval1 * i);
				//	std::cout << "allbetas[" << x << "] is " << allbetas[x] << "\n";
					x++;
					break;
				case HGEOMETRIC:
					allbetas[x] = 1 - (1 - hval2) * (i) * 
					pow (hval1, (double) (numprocesses * numchains - 1 - (i))) / (double) (numprocesses * numchains - 1);
					x++;
					break;
				case HEVEN:
					h = 1.0 / (numchains - 1);
					if (i == 0) {
						allbetas[x] = 1.0;
					} else {
						if (i == numchains * numprocesses - 1) {
							allbetas[x] = 0.0;
						} else {
							allbetas[x] = 1.0 - i * h;
						}
						x++;
						break;
					}
				}
		}
					
//	}
  
  
/*  9/26/2011 thermodynamic integration handled by simpson's rule which requires an even number of intervals 
    this means we need numchains to be odd, so if   numchains is even,  we reduce it by 1  */
  if (heatmode == HEVEN  && ODD(numchains) == 0 && numprocesses == 1)
    numchains -= 1; 

  int i = 0;
  for (ci = currentid * numchains; ci < currentid * numchains + numchains; ci++)
  {
    switch (heatmode)
    {
    case HLINEAR:
	///AS: Changing this to MC3 initialization, based on current process ID
     // beta[ci] = 1.0 / (1.0 + hval1 * ci * currentid);
      //Assuming here that hval is the \delta T parameter as specified in MrBayes, Altekar et al
     // if (hval1 == 0)
     if (hval1 < 0.05)
	hval1 = 0.05;
      beta[i] = 1.0 / (1.0 + hval1 * ci);
	betaref1[i] = beta[i];
	betaref2[i] = beta[i];
	/*if (ci == 1 && currentid == 0) {
		beta[ci] = 1.0;
	}*/
	///AS: Debug only
	//std::cout << "Temperature of chain " << ci << " is " << beta[i] << " and currentid is " << currentid << "\n";
      break;
	///AS: Not sure about geometric or thermodynamic just as yet 
   /* case HTWOSTEP: stopped using 8/23/2011 */ 
    case HGEOMETRIC:
      /* sometimes, with hval 1 values > 1 get identical adjacent beta values */ 
      beta[i] =
        1 - (1 - hval2) * (ci) * pow (hval1, (double) (numprocesses * numchains - 1 - (ci))) / (double) (numprocesses * numchains - 1);
	betaref1[i] = beta[i];
	betaref2[i] = beta[i];
	//std::cout << "Temperature of chain " << ci << " is " << beta[i] << "\n";
      break;
    /* 5/19/2011 JH adding thermodynamic integration */
    case HEVEN:
      h = 1.0 / (numprocesses * numchains-1);
      if (ci== 0) {
        beta[i] = 1.0;
	betaref1[i] = beta[i];
	betaref2[i] = beta[i];

	//std::cout << "Temperature of chain " << ci << " is " << beta[i] << "\n";
	}
      else
      {
        if (ci== (numprocesses * numchains-1)) {
          beta[i] = 0.0;
	betaref1[i] = beta[i];
	betaref2[i] = beta[i];

       } else {
          beta[i] = 1.0 - ci*h;
		betaref1[i] = beta[i];
		betaref2[i] = beta[i];
	}

    }
      break;
    }
     /* 5/19/2011 JH adding thermodynamic integration */
    if (calcoptions[CALCMARGINALLIKELIHOOD])
    {
      if (beta[i] < 0.0 || beta[i] > 1.0)
        IM_err (IMERR_COMMANDLINEHEATINGTERMS, "command line heating terms have caused a heating value out of range. chain %d beta %lf", ci, beta[i]);
    }
    else
    {
    //JH 6/11/2010  added this, mostly in case users use h1>1 (which may be ok but can also easily give negative values for beta)
    if (beta[i] <= 0.0 || beta[i] > 1.0)
      IM_err (IMERR_COMMANDLINEHEATINGTERMS, "command line heating terms have caused a heating value out of range. chain %d beta %lf", ci, beta[i]);
  }
	i++;
  }
}                               /* setheat */

/* swaps chains,  adjust heating levels if adaptive heating is invoked.
will do multiple swaps if swaptries > 1.  If a swap was done involving chain0 then swap0ok =1. Sometimes, with 
swaptries > 1,  chain 0 will swap with another chain, and then swap back, restoring the current parameter 
values and genealogies. This is not detected, so stuff later that checks the return from this function
will think that parameters have changed.  This should not matter */

/* CR 110929.4 get rid of extraneous args in declaration  and remove several 
 * variables not being used */
///AS: Swapchains will swap entire chain structures. Instead for the Parallel MC3 framework, we need to only swap beta values
//AS: So I am rewriting a new function, called swapbetas instead.


int
swapchains_bwprocesses(/*int *swapper, int *swappee, */int current_id, int step, int swaptries, int swapbetasonly, int chainduration,
			int burnduration,/* std::ofstream &f1,*/ int swapA, int swapB)
{
	int swapvar = 0;
	#ifdef MPI_ENABLED	
	
	MPI::Status status;
	MPI::Request request[2];
	double metropolishastingsterm;
	int whichElementA, whichElementB;
	int procIdForA, procIdForB;
	int doISwap, areWeA;
	double sumi = 0;
	double sumj = 0;
	int aiters = 0;
	int biters = 0;
	double abeta = 0.0;
	double bbeta = 0.0;
	int p = 0;
	int q = 0;
	whichElementA = 0;
	whichElementB = 0;
		/* OLD CODE: AS - changing this to pick the chains to swap instead of the processes */
		/* as on 7/1/2014 
		procIdForA = swapA;
		procIdForB = swapB;
		*/
		procIdForA = floor (swapA/numchains);
		procIdForB = floor (swapB/numchains);
			
		whichElementA = swapA - procIdForA * numchains;
		whichElementB = swapB - procIdForB * numchains;
		
/*	do {
		whichElementA = (int) (uniform () * numchains);
	} while (whichElementA < 0 || whichElementA >= numchains);
	
	do {
		whichElementB = (int) (uniform () * numchains);
	} while (whichElementB < 0 || whichElementB >= numchains);
*/
	
	//f1 << "ProcID for A is " << procIdForA << " and ProcID for B is " << procIdForB << "\n";
	doISwap = areWeA = 0;
	if (current_id == procIdForA) {
		doISwap = 1;
		areWeA = 1;
	} else if (current_id == procIdForB) {
		doISwap = 1;
	}
	if (doISwap == 1) {
	
	if (procIdForA == procIdForB) {
	//	f1 << "Pushing this to within process swap...\n Run done? " << rundone;
		swapchains(swaptries, swapbetasonly, current_id);
		return swapvar;
	} else {
	//	f1 << "Swapper and swappee are not the same so I am going to attempt swap\n";
		if (areWeA == 1) {
			if (procIdForA < procIdForB) {
				swaps_bwprocesses[procIdForB][procIdForA]++;
			} else {
				swaps_bwprocesses[procIdForA][procIdForB]++;
			}
			abeta = beta[whichElementA];
			sumi = calcpartialswapweight(whichElementA);		
		//	f1 << "Chain is " << current_id << " Step = " << step << " abeta = " << beta[whichElementA] << " sumi = " << sumi << "\n";
			try {
				request[0] =  MPI::COMM_WORLD.Isend(&abeta, 1, MPI::DOUBLE, procIdForB, 0);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[1] = MPI::COMM_WORLD.Irecv(&bbeta, 1, MPI::DOUBLE, procIdForB, 1);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				MPI::Request::Waitall(2, request);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[0] = MPI::COMM_WORLD.Isend(&sumi, 1, MPI::DOUBLE, procIdForB, 2);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[1] = MPI::COMM_WORLD.Irecv(&sumj, 1, MPI::DOUBLE, procIdForB, 3);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
	//			f1 << "Going into wait since I am A\n";
				MPI::Request::Waitall(2, request);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			//AS: adding swap counting to tempbasedswapcount
		
	
			for (p = 0; p < numprocesses * numchains; p++) {
				if (allbetas[p] == abeta) {
					break;
				}
			}
			for (q = 0; q < numprocesses * numchains; q++) {
				if (allbetas[q] == bbeta) {
					break;
				}
			}
			/*
			if (p < q) {
				tempbasedswapcount[q][p]++;
			} else {
				tempbasedswapcount[p][q]++;
			}*/
			//AS: only swapper keeps track of swap attempts and successes				



	//			f1 << "Done waiting since I am A\n";
		}
		if (areWeA != 1) {
			bbeta = beta[whichElementB];
		//	std::cout << "Bbeta is " << bbeta << "\n";
			sumj = calcpartialswapweight(whichElementB);
			//f1 << "Chain is " << current_id << " Step = " << step << " bbeta = " << beta[whichElementB] << " sumj = " << sumj << "\n";
		
			try {
				request[0] =  MPI::COMM_WORLD.Isend(&bbeta, 1, MPI::DOUBLE, procIdForA, 1);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[1] = MPI::COMM_WORLD.Irecv(&abeta, 1, MPI::DOUBLE, procIdForA, 0);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				MPI::Request::Waitall(2, request);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[0] = MPI::COMM_WORLD.Isend(&sumj, 1, MPI::DOUBLE, procIdForA, 3);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
				request[1] = MPI::COMM_WORLD.Irecv(&sumi, 1, MPI::DOUBLE, procIdForA, 2);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
	//			f1 << "Going into wait since I am B\n";
				MPI::Request::Waitall(2, request);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
	//			f1 << "Done waiting since I am B\n";
		}
		swapvar = 0;
		//AS: Debug only		
		//f1 << "Chain is " << current_id << " Step = " << step << " abeta = " << abeta << " sumi = " << sumi << "\n";
		//f1 << "Chain is " << current_id << " Step = " << step << " bbeta = " << bbeta << " sumj = " << sumj << "\n";
		if (areWeA == 1) {
			metropolishastingsterm = swapweight_bwprocesses(sumi, sumj, abeta, bbeta);
			if (metropolishastingsterm >= 1.0 || metropolishastingsterm > uniform ()) {
	//AS: Debug only
	//			f1 << "Swapper " << procIdForA << " here, swapping with " << procIdForB << "\n";
	//			f1 << "Old beta was: " << beta[whichElementA] << "\n";
				beta[whichElementA] = bbeta;
	//			f1 << "New beta is: " << beta[whichElementA] << "\n";
				if (procIdForA < procIdForB) {
					swaps_bwprocesses[procIdForA][procIdForB]++;
				} else {
					swaps_bwprocesses[procIdForB][procIdForA]++;
				}
				//AS: only swapper keeps track of swap
				/*
				if (p < q) {
					tempbasedswapcount[p][q]++;
				} else {
					tempbasedswapcount[q][p]++;
				}
				*/
				swapvar = 1;
			} else {
				swapvar = 0;
	//			f1 << "MH term is < 1 or < uniform! So can't swap!\n"; 	
			}
			
			try {
				request[0] = MPI::COMM_WORLD.Isend(&swapvar, 1, MPI::INT, procIdForB, 1);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
	//			f1 << "Swap signal being sent! so waiting...\n";
				MPI::Request::Waitall(1, &request[0]);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			
		}
		if (areWeA != 1) {
			
			try {
				request[0] = MPI::COMM_WORLD.Irecv(&swapvar, 1, MPI::INT, procIdForA, 1);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			try {
	//			f1 << "Swap signal being received! so waiting...\n";
				MPI::Request::Waitall(1, &request[0]);
			} catch (MPI::Exception e) {
				std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
				MPI::COMM_WORLD.Abort(-1);
				return 0;
			}
			//metropolishastingsterm = swapweight_bwprocesses(sumi, sumj, abeta, bbeta);
			//if (metropolishastingsterm >= 1.0 || metropolishastingsterm > uniform ()) {
			if (swapvar != 0) {
	//AS: Debug only
	//			f1 << "Swappee " << procIdForB << " here, swapping with " << procIdForA << "\n";
	//			f1 << "Old beta was " << beta[whichElementB] << "\n";
				beta[whichElementB] = abeta;
	//			f1 << "New beta is " << beta[whichElementB] << "\n";
			} else {
	//			f1 << "MH term is < 1 or < uniform! So can't swap!\n"; 	
			}
		}
	}
	}
	#endif
	return swapvar;	

}


int
swapchains (int swaptries, int swapbetasonly, int currentid)
{
  int ci, cj, i, swap0ok;
  double metropolishastingsterm;
  void *swapptr;
  int cjmin, cjrange;
  int p = 0;
  int q = 0;
#define  MINSWAP  0.1
#define  BETADJUST  1.414
#define  INCADJUST  1.414
#define  MINHEAT  0.0001
#define  MAXHEAT  0.2
#define  MAXINC  100
#define  MININC  0.1
#define  PAUSESWAP 1000
#define SWAPDIST 7
// 5/27/2010  removed HADAPT stuff
//std::cout << "Within process swapping....\n";
  for (i = 0, swap0ok = 0; i < swaptries; i++)
  {

    do
    {
      ci = (int) (uniform () * numchains);
    } while (ci < 0 || ci >= numchains);

    if (numchains < 2*SWAPDIST + 3)
    {
      cjmin = 0;
      cjrange = numchains;
    }
    else
    {
      cjmin = IMAX(0,ci-SWAPDIST);
      cjrange = IMIN(numchains, ci+SWAPDIST) -cjmin;
    }
    do
    {
      cj = cjmin + (int) (uniform () * cjrange);
    } while (cj == ci || cj < 0 || cj >= numchains);

	//AS: adding swapcounting based on temperatures
	for (p = 0; p < numprocesses * numchains; p++) {
		if (allbetas[p] == beta[ci]){
			break;
		}
	}
	for (q = 0; q < numprocesses * numchains; q++) {
		if (allbetas[q] == beta[cj]){
			break;
		}
	}
	/*
	if (p < q) {
		tempbasedswapcount[q][p]++;
	} else {
		tempbasedswapcount[p][q]++;
	}	
	*/


    if (ci < cj)
    {
      swapcount[cj][ci]++;
	//if (numprocesses > 1) {
//		swapcount_bwprocesses[currentid * numchains + cj][currentid * numchains + ci]++;
	//}
    }
    else
    {
      swapcount[ci][cj]++;
	//if (numprocesses > 1) {
//		swapcount_bwprocesses[currentid * numchains + ci][currentid * numchains + cj]++;
	//}
    }
    metropolishastingsterm = swapweight (ci, cj);
    if (metropolishastingsterm >= 1.0
        || metropolishastingsterm > uniform ())

    {
	///AS: Either I swap only temperatures, or I swap all pointer
	///AS: I am going to denote this by some global variable, perhaps should be part of modeloptions
	//AS: For now, let's just call it swaptempsonly
	if (swapbetasonly) {
		swapbetas(ci, cj);
		///AS: Debug only
		//std::cout << "Temperatures of chains " << ci << " and " << cj << " have been swapped!\n";
	} else {
//	std::cout << "Swapping pointers instead...\n";
      swapptr = C[ci];
      C[ci] = C[cj];
      C[cj] = static_cast<chain *> (swapptr);
	}
      if (ci < cj)

      {
        swapcount[ci][cj]++;
	//if (numprocesses > 1) {
//		swapcount_bwprocesses[currentid * numchains + ci][currentid * numchains + cj]++;
	//}
      }
      else
      {
        swapcount[cj][ci]++;
	//if (numprocesses > 1) {
//		swapcount_bwprocesses[currentid * numchains + cj][currentid * numchains + ci]++;
	//}
      }
	/*
	if (p < q) {
		tempbasedswapcount[p][q]++;
	} else {
		tempbasedswapcount[q][p]++;
	}
	*/	


      if (ci == 0 || cj == 0)
        swap0ok |= 1;
      }
 }
  return swap0ok;
}                               /* swapchains */

void
swapbetas (int ci, int cj) {
	double btemp = 0.0;
	btemp = beta[ci];
	beta[ci] = beta[cj];
	beta[cj] = btemp;
	return;
}				/* swapbetas */



void
printchaininfo (FILE * outto, int heatmode, double hval1,
                double hval2, int currentid)
{
  int i;
  if (currentid == 0) {
  fprintf (outto, "\nCHAIN SWAPPING BETWEEN SUCCESSIVE CHAINS: ");
  switch (heatmode)

  {
  case HLINEAR:
    fprintf (outto, " Linear Increment  term: %.4f\n", hval1);
    break;
  /*case HTWOSTEP:
    fprintf (outto, " Twostep Increment  term1: %.4f term2: %.4f\n", hval1,
             hval2);
    break;  stopped using 8/23/2011 */
  case HGEOMETRIC:
    fprintf (outto, " Geometric Increment  term1: %.4f term2: %.4f\n",
             hval1, hval2);
    break;
  case HEVEN:
    fprintf (outto, "\n");
  }
  fprintf (outto,
           "-----------------------------------------------------------------------------\n");
 }


//AS: here I am going to reduce the swapcount matrices - regardless of stdout or file output
//modified on 12/9/2014

	#ifdef MPI_ENABLED
	//if (numprocesses > 1 && (step / ((int) printint * (int) printint)) == step && step > 0) {
		//std::cout << "Going into reduction in intervaloutput\n";
		if (numprocesses > 1) {
		for (int x = 0; x < numprocesses; x++) {
			for (int y = 0; y < numprocesses; y++) {
				try {
					MPI::COMM_WORLD.Reduce(&swaps_bwprocesses[x][y], &swaps_rec_bwprocesses[x][y], 1,
					MPI::INT, MPI::SUM, 0);
				} catch (MPI::Exception e) {
					std::cout << e.Get_error_string() << e.Get_error_code() << "\n";
					MPI::COMM_WORLD.Abort(-1);
				}
			}
		}
		//AS: also have to send-receive all the swapcount variables into the swapcount_bwprocesses matrix
		if (currentid == 0) {
			for (int x = 0; x < numchains; x++) {
				for (int y = 0; y < numchains; y++) {
					swapcount_bwprocesses[x][y] = swapcount[x][y];
				}
			}
		}	
		for (int x = 1; x < numprocesses; x++) {
			if (currentid == x) {
				for (int y = 0; y < numchains; y++) {
					for (int z = 0; z < numchains; z++) {
						MPI::COMM_WORLD.Send(&swapcount[y][z], 1, MPI::INT, 0, 1234);
					}
				}
			}
			if (currentid == 0) {
				for (int y = 0; y < numchains; y++) {
					for (int z = 0; z < numchains; z++) {
						MPI::COMM_WORLD.Recv(&swapcount_bwprocesses[x * numchains + y][x * numchains + z], 1, MPI::INT, x, 1234);
					}
				}
			}
		}
			
	}
		//std::cout << "Done with reduction in interval output\n";
	#endif


  if (outto == stdout)

  {
    if (currentid == 0) {
    fprintf (outto, "beta terms :");
    for (i = 0; i < numprocesses * numchains; i++)
	//AS: this has to change...!!
      fprintf (outto, "|%2d %5.3f", i, allbetas[i]);
    fprintf (outto, "\n");
	if (currentid == 0) {
    fprintf (outto, "Swap rates :");
    for (i = 0; i < numprocesses * numchains - 1; i++)
      //if (swapcount_bwprocesses[i + 1][i])
        fprintf (outto, "|%2d %5.3f", i,
                 swapcount_bwprocesses[i][i + 1] / (float) swapcount_bwprocesses[i + 1][i]);
    fprintf (outto, "\n");
    fprintf (outto, "Swap counts:");
    for (i = 0; i < numprocesses * numchains - 1; i++)
      fprintf (outto, "|%2d %5ld", i, swapcount_bwprocesses[i][i + 1]);
    fprintf (outto, "\n\n");
	}
	}
  }
  else
  {
	if (numprocesses > 1 && currentid == 0) {
    fprintf (outto, "Chain   #Swaps  Rate \n");
    for (i = 0; i < numprocesses * numchains - 1; i++)

    {
  //    if (swapcount_bwprocesses[i + 1][i])
        fprintf (outto, " %3d  %5ld  %7.4f\n", i,
                 swapcount_bwprocesses[i][i + 1],
                 swapcount_bwprocesses[i][i + 1] / (float) swapcount_bwprocesses[i + 1][i]);

    //  else
      //  fprintf (outto, " %3d  %7.4f %5ld  \n", i, allbetas[i],
        //         swapcount_bwprocesses[i][i + 1]);
    }
    fprintf (outto, " %3d  na      na\n", i);
    fprintf (outto, "\n");
	}
  }
  if (numprocesses > 1 && currentid == 0) {
	//AS: this has to be allreduced before printing
	fprintf (outto, "\nCHAIN SWAPPING BETWEEN PROCESSES:\n");
	fprintf (outto,
		"------------------------------------------------\n");
	
	fprintf (outto, "Processor1  Processor2  #SwapAttempts\n");
	for (i = 0; i < numprocesses; i++) {
		for (int j = 0; j < numprocesses; j++) {
			if (i > j) {
				fprintf (outto, " %3d      %3d     %5ld\n", i, j, swaps_rec_bwprocesses[i][j]);
			}
		}
	} 
	fprintf (outto, "\n\n");
	fprintf (outto, "Processor1  Processor2  #Swaps\n");
	for (i = 0; i < numprocesses; i++) {
		for (int j = 0; j < numprocesses; j++) {
			if (i < j) {
				fprintf (outto, " %3d      %3d      %5ld\n", i, j, swaps_rec_bwprocesses[i][j]);
			}
		}
	}
	fprintf (outto, "\n\n");
				
				
			
	/*
	fprintf (outto, "\nCHAIN SWAPPING BETWEEN TEMPERATURES:\n");
	fprintf (outto,
		"------------------------------------------------\n");
	fprintf (outto, "Temperature1  Temperature2  SwapRate  SwapAttempts\n");
	for (i = 0; i < numprocesses * numchains; i++) {
		for (int j = 0; j < numprocesses * numchains; j++) {
			if (i > j && tempbased_rec_swapcount[i][j] > 0) {
				fprintf (outto, " %7.4f    %7.4f    %7.4f    %5ld\n", allbetas[i], allbetas[j], 
				(float)(tempbased_rec_swapcount[j][i]/tempbased_rec_swapcount[i][j]), tempbased_rec_swapcount[i][j]);
			}
			if (i > j && tempbased_rec_swapcount[i][j] == 0) {
				fprintf (outto, "%7.4f    %7.4f    na    0\n", allbetas[i], allbetas[j]);
			}
		}
	}*/
/*
	fprintf (outto, "\n\n");
	fprintf (outto, "Temperature1  Temperature2  #Swaps\n");
	for (i = 0; i < numprocesses * numchains; i++) {
		for (int j = 0; j < numprocesses * numchains; j++) {
			if (i < j) {
				fprintf (outto, " %7.4f    %7.4f   %5ld\n", allbetas[i], allbetas[j], tempbased_rec_swapcount[i][j]);
			}
		}
	}*/
//	fprintf (outto, "\n\n");
	}


}                               /* printchaininfo */
