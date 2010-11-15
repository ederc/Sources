/*****************************************************************************\
 * Computer Algebra System SINGULAR    
\*****************************************************************************/
/** @file f5c.cc
 * 
 * Implementation of variant F5e of Faugere's
 * F5 algorithm in the SINGULAR kernel. F5e reduces the computed Groebner 
 * bases after each iteration step, whereas F5 does not do this.
 *
 * ABSTRACT: An enhanced variant of Faugere's F5 algorithm .
 *
 * LITERATURE:
 * - F5 Algorithm:  http://www-calfor.lip6.fr/~jcf/Papers/F02a.pdf
 * - F5C Algorithm: http://arxiv.org/abs/0906.2967
 * - F5+ Algorithm: to be confirmed
 *
 * @author Christian Eder
 *
 * @internal @version \$Id$
 *
 **/
/*****************************************************************************/

#include <kernel/mod2.h>
#ifdef HAVE_F5C
#include <unistd.h>
#include "options.h"
#include "structs.h"
#include "omalloc.h"
#include "polys.h"
#include "timer.h"
#include "p_polys.h"
#include "p_Procs.h"
#include "ideals.h"
#include "febase.h"
#include "kstd1.h"
#include "khstd.h"
#include "kbuckets.h"
#include "weight.h"
#include "intvec.h"
#include "prCopy.h"
#include "p_MemCmp.h"
#include "pInline2.h"
#include "f5c.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#ifdef PDEBUG
#undef PDEBUG
#define PDEBUG 3 
#endif
#define F5EDEBUG  1 
#define setMaxIdeal 64
#define NUMVARS currRing->ExpL_Size
int create_count_f5 = 0; // for KDEBUG option in reduceByRedGBCritPair

/// NOTE that the input must be homogeneous to guarantee termination and
/// correctness. Thus these properties are assumed in the following.
ideal f5cMain(ideal F, ideal Q) 
{
  Print("TEST1\n");
  if(idIs0(F)) 
  {
    return idInit(1, F->rank);
  }
  // interreduction of the input ideal F
  ideal FRed      = idCopy( F );
  //ideal FRed      = F;
  ideal FRedTemp  = kInterRed( FRed );
  idDelete( &FRed );
  FRed            = FRedTemp;

#if F5EDEBUG
  int j = 0;
  int k = 0;
  int* expVec   = new int[(currRing->N)+1];
  for( ; k<IDELEMS(FRed); k++)
  {
    Print("ORDER: %ld\n",FRed->m[k]->exp[currRing->pOrdIndex]);
    Print("SIZE OF INTERNAL EXPONENT VECTORS: %d\n",currRing->ExpL_Size);
    pGetExpV(FRed->m[k],expVec);
    Print("EXP VEC: ");
    for( ; j<currRing->N+1; j++)
    {
      Print("%d  ",expVec[j]);
    }
    j = 0;
    Print("\n");
  }
#endif
  /// define the global variables for fast exponent vector
  /// reading/writing/comparison
  int i = 0;
  /// declaration of global variables used for exponent vector
  int numVariables  = currRing->N;
  /// reading/writing/comparison
  int* shift              = (int*) omAlloc((currRing->N+1)*sizeof(int));
  unsigned long* negBitmaskShifted  = (unsigned long*) omAlloc((currRing->N+1)*sizeof(unsigned long));
  int* offsets            = (int*) omAlloc((currRing->N+1)*sizeof(int));
  const unsigned long _bitmasks[4] = {-1, 0x7fff, 0x7f, 0x3};
  for( ; i<currRing->N+1; i++)
  {
    /*
     shift[i]                = (currRing->VarOffset[i] >> 24) & 0x3f;
    negBitmaskShifted[i]    = ~((currRing->bitmask & 
                                _bitmasks[(currRing->VarOffset[i] >> 30)]) 
                                << shift[i]);
    offsets[i]              = (currRing->VarOffset[i] & 0xffffff);
    */
    shift[i]                = currRing->VarOffset[i] >> 24;
    negBitmaskShifted[i]    = ~(currRing->bitmask << shift[i]);
    offsets[i]              = (currRing->VarOffset[i] & 0xffffff);
  }
  
  ideal r = idInit(1, FRed->rank);
  // save the first element in ideal r, initialization of iteration process
  r->m[0] = FRed->m[0];
  Print("1st element: ");
  pWrite( pHead(r->m[0]) );
  // counter over the remaining generators of the input ideal F
  for(i=1; i<IDELEMS(FRed); i++) 
  {
    // computation of r: a groebner basis of <F[0],...,F[gen]> = <r,F[gen]>
    r = f5cIter(FRed->m[i], r, numVariables, shift, negBitmaskShifted, offsets);
    // the following interreduction is the essential idea of F5e.
    // NOTE that we do not need the old rules from previous iteration steps
    // => we only interreduce the polynomials and forget about their labels
#if F5EDEBUG
/*    for( k=0; k<IDELEMS(r); k++ )
    {
      pTest( r->m[k] );
      Print("TESTS bef interred: %p ",r->m[k]);
      //pWrite(r->m[k]);
    }
*/
#endif
    Print("aHERE1\n");
    ideal rTemp = kInterRed(r);
    idDelete( &r );
    r = rTemp;
    Print("HERE2\n");
#if F5EDEBUG
/*    for( k=0; k<IDELEMS(r); k++ )
    {
      pTest( r->m[k] );
      Print("TESTS after interred: %p ",r->m[k]);
      //pWrite(r->m[k]);
    }
*/
#endif
  }
  omFree(shift);
  omFree(negBitmaskShifted);
  omFree(offsets);
  return r;
}



ideal f5cIter ( poly p, ideal redGB, int numVariables, int* shift, 
                unsigned long* negBitmaskShifted, int* offsets
              )
{
#if F5EDEBUG
  Print("F5CITER-BEGIN\n");
  Print("NEXT ITERATION ELEMENT: ");
  pWrite( pHead(p) );
  Print("ORDER %ld -- %ld\n",p_GetOrder(p,currRing), p->exp[currRing->pOrdIndex]);
  int j = 0;
  int k = 0;
  Print("SIZE OF redGB: %d\n",IDELEMS(redGB));
  for( ; k<IDELEMS(redGB); k++)
  {
    j = 0;
    Print("Poly: %p -- ",redGB->m[k]);
    pTest( redGB->m[k] );
   // //pWrite(pHead(redGB->m[k]));
    Print("%d. EXP VEC: ", k);
    Print("\n");
  }
#endif  
  // create the reduction structure "strat" which is needed for all 
  // reductions with redGB in this iteration step
  kStrategy strat = new skStrategy;
  strat->syzComp  = 0;
  strat->ak       = idRankFreeModule(redGB);
  prepRedGBReduction(strat, redGB);
#if F5EDEBUG
  Print("F5CITER-AFTER PREPREDUCTION\n");
  Print("ORDER %ld -- %ld\n",p_GetOrder(p,currRing), p->exp[currRing->pOrdIndex]);
  Print("SIZE OF redGB: %d\n",IDELEMS(redGB));
  for(k=0 ; k<IDELEMS(redGB); k++)
  {
    j = 0;
    //Print("Poly: %p -- ",redGB->m[k]);
    //pTest( redGB->m[k] );
    ////pWrite(pHead(redGB->m[k]));
    Print("%d. EXP VEC: ", k);
    Print("\n");
  }
#endif  
  int i;
  // create array of leading monomials of previously computed elements in redGB
  F5Rules* f5Rules        = (F5Rules*) omAlloc(sizeof(struct F5Rules));
  Print("ffff\n");
  pTest( p );
  Print("ffff0\n");
  // malloc memory for slabel
  Print("ELEMENTS IN REDGB: %d\n",IDELEMS(redGB) );
  f5Rules->label  = (int**) omAlloc(IDELEMS(redGB)*sizeof(int*));
  Print("ffff\n");
  pTest( p );
  Print("ffff1\n");
  f5Rules->slabel = (unsigned long*) omAlloc0(IDELEMS(redGB)*
                    sizeof(unsigned long)); 
  Print("ALLOCATED SLABEL: %p -- %p / %p\n",f5Rules->slabel,f5Rules->slabel[0],f5Rules->slabel[1]);
  pTest( redGB->m[0] );

  for(i=0; i<IDELEMS(redGB); i++) 
  {
    ////pWrite( redGB->m[i] );
    Print("here again\n");
    Print("%d -- ",i);
    f5Rules->label[i]  =  (int*) omAlloc((currRing->N+1)*sizeof(int));
    pGetExpV(redGB->m[i], f5Rules->label[i]);
    pWrite( pHead(redGB->m[i]) );
    Print("ADDR: %p\n",f5Rules->slabel[i] );
    Print("ADDR: %p\n",*(f5Rules->slabel+i) );
    f5Rules->slabel[i] =  pGetShortExpVector(redGB->m[i]); // bit complement ~
    Print("VALUE: %ld\n%d -- ",f5Rules->slabel[i],i);
    //pWrite( redGB->m[i] );
    pTest( redGB->m[i] );
    Print("------\n");
    ////pWrite( redGB->m[i] );
  } 
  Print("ffff\n");
  //pLmTest( redGB->m[0] );
  Print("ffff2\n");

  f5Rules->size = i++;
  // initialize a first (dummy) rewrite rule for the initial polynomial of this
  // iteration step
  int* firstRuleLabel = (int*) omAlloc0( (currRing->N+1)*sizeof(int) );
  RewRules firstRule  = { NULL, firstRuleLabel, 0 };
  // reduce and initialize the list of Lpolys with the current ideal generator p
  Print("ffff\n");
  pTest( p );
  Print("ffff3\n");
  p = reduceByRedGBPoly( p, strat );
  //pWrite( p );
  if( p )
  {
    pNorm( p );  
    Lpoly* gCurr      = (Lpoly*) omAlloc( sizeof(Lpoly) );
    gCurr->next       = NULL;
    gCurr->sExp       = pGetShortExpVector(p);
    gCurr->p          = p;
    gCurr->rewRule    = &firstRule;
    gCurr->redundant  = FALSE;
    
    // initializing the list of critical pairs for this iteration step 
    CpairDegBound* cpBounds = NULL;
    criticalPairInit( gCurr, redGB, *f5Rules, &cpBounds, numVariables, shift,
                      negBitmaskShifted, offsets
                    ); 
    if( cpBounds )
    {
      computeSpols( strat, cpBounds, redGB, &gCurr, f5Rules, numVariables, shift, 
                    negBitmaskShifted, offsets
                  );
    }
    // delete the reduction strategy strat since the current iteration step is
    // completed right now
    clearStrat( strat, redGB );
    omFree( firstRuleLabel );
    for( i=0; i<IDELEMS(redGB); i++ )
    {
      omFree( f5Rules->label[i] );
    }
    omFree( f5Rules->slabel );
    omFree( f5Rules->label );
    omFree( f5Rules );
    // next all new elements are added to redGB & redGB is being reduced
    Lpoly* temp;
    int counter = 1;
    while( gCurr )
    {
      if( p_GetOrder( gCurr->p, currRing ) == pWTotaldegree( gCurr->p, currRing ) )
      {
        Print(" ALLES OK\n");
      }
      else 
      {
        Print("BEI POLY "); gCurr->p; Print(" stimmt etwas nicht!\n");
      }
      Print("%d INSERT TO REDGB: ",counter);
      //pWrite(gCurr->p);
      idInsertPoly( redGB, gCurr->p );
      temp = gCurr;
      gCurr = gCurr->next;
      omFree(temp);
      counter++;
    }
    idSkipZeroes( redGB );
  }
#if F5EDEBUG
  Print("F5CITER-END\n");
#endif  
  return redGB;
}



void criticalPairInit ( Lpoly* gCurr, const ideal redGB, 
                        const F5Rules& f5Rules,  
                        CpairDegBound** cpBounds, int numVariables, int* shift, 
                        unsigned long* negBitmaskShifted, int* offsets
                      )
{
#if F5EDEBUG
  Print("CRITPAIRINIT-BEGINNING\n");
#endif
  int i, j;
  int* expVecNewElement = (int*) omAlloc((currRing->N+1)*sizeof(int));
  int* expVecTemp       = (int*) omAlloc((currRing->N+1)*sizeof(int));
  pGetExpV(gCurr->p, expVecNewElement); 
  // this must be changed for parallelizing the generation process of critical
  // pairs
  Cpair* cpTemp;
  Cpair* cp = (Cpair*) omAlloc( sizeof(Cpair) );
  cpTemp    = cp;
  cpTemp->next      = NULL;
  cpTemp->mLabelExp = NULL;
  cpTemp->mLabel1   = NULL;
  cpTemp->smLabel1  = 0;
  cpTemp->mult1     = NULL;
  cpTemp->p1        = gCurr->p;
  cpTemp->rewRule1  = gCurr->rewRule;
  Print("TEST REWRULE1 %p\n",cpTemp->rewRule1);
  cpTemp->mLabel2   = NULL;
  cpTemp->smLabel2  = 0;
  cpTemp->mult2     = NULL;
  cpTemp->p2        = NULL;

  // we only need to alloc memory for the 1st label as for the initial 
  // critical pairs all 2nd generators have label NULL in F5e
  cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
  cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  int temp;
  long critPairDeg; // degree of the label of the pair in the following
  for(i=0; i<IDELEMS(redGB)-1; i++)
  {
    pGetExpV(redGB->m[i], expVecTemp); 
    Print("EXPVECTEMP[0]: %d\n",expVecTemp[0]);
    // computation of the lcm and the corresponding multipliers for the critical
    // pair generated by the new element and elements of the previous iteration
    // steps, i.e. elements already in redGB
    cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = 0; 
    cpTemp->mult2[0]    = 0; 
    critPairDeg = 0;
    for(j=1; j<=currRing->N; j++)
    {
      temp  = expVecNewElement[j] - expVecTemp[j];
      // note that the label of the first element in gCurr is 0
      // thus gCurr.label is no longer mentioned in the following
      // computations
      if(temp<0)
      {
        cpTemp->mult1[j]    =   -temp;  
        cpTemp->mult2[j]    =   0; 
        cpTemp->mLabel1[j]  =   - temp;
        critPairDeg         +=  (- temp); 
      }
      else
      {
        cpTemp->mult1[j]    =   0;  
        cpTemp->mult2[j]    =   temp;  
        cpTemp->mLabel1[j]  =   0;
      }
    }
    cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
    
    // testing the F5 Criterion
    if(!criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules)) 
    {
      // completing the construction of the new critical pair and inserting it
      // to the list of critical pairs 
      cpTemp->p2        = redGB->m[i];
      // now we really need the memory for the exp label
      cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                                sizeof(unsigned long));
      Print("CRITPAIR EXPTEMP 0: %ld\n",cpTemp->mLabelExp[0]);
      getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                          numVariables, shift, negBitmaskShifted, offsets );
      Print("CRITPAIR EXPTEMP 0: %ld\n",cpTemp->mLabelExp[0]);
      insertCritPair(cpTemp, critPairDeg, cpBounds);
      Cpair* cp = (Cpair*) omAlloc( sizeof(Cpair) );
      cpTemp            = cp;
      cpTemp->next      = NULL;
      cpTemp->mLabelExp = NULL;
      cpTemp->mLabel1   = NULL;
      cpTemp->smLabel1  = 0;
      cpTemp->mult1     = NULL;
      cpTemp->p1        = gCurr->p;
      cpTemp->rewRule1  = gCurr->rewRule;
      cpTemp->mLabel2   = NULL;
      cpTemp->smLabel2  = 0;
      cpTemp->mult2     = NULL;
      cpTemp->p2        = NULL;

      cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
      cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
      cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
    }
  }
  // same critical pair processing for the last element in redGB
  // This is outside of the loop to keep memory low, since we know that after
  // this element no new memory for a critical pair must be allocated.
  pGetExpV(redGB->m[IDELEMS(redGB)-1], expVecTemp); 
  // computation of the lcm and the corresponding multipliers for the critical
  // pair generated by the new element and elements of the previous iteration
  // steps, i.e. elements already in redGB
  cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = pGetComp(cpTemp->p1); 
  cpTemp->mult2[0]    = pGetComp(redGB->m[IDELEMS(redGB)-1]); 
  critPairDeg = 0;
  for(j=1; j<=currRing->N; j++)
  {
    temp  = expVecNewElement[j] - expVecTemp[j];
    // note that the label of the first element in gCurr is 0
    // thus gCurr.label is no longer mentioned in the following
    // computations
    if(temp<0)
    {
      cpTemp->mult1[j]    =   -temp;  
      cpTemp->mult2[j]    =   0; 
      cpTemp->mLabel1[j]  =   -temp;
      critPairDeg         +=  (- temp);
    }
    else
    {
      cpTemp->mult1[j]    =   0;  
      cpTemp->mult2[j]    =   temp;  
      cpTemp->mLabel1[j]  =   0;
    }
  }
  cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
  // testing the F5 Criterion
  if(!criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules)) 
  {
    // completing the construction of the new critical pair and inserting it
    // to the list of critical pairs 
    cpTemp->p2        = redGB->m[IDELEMS(redGB)-1];
    // now we really need the memory for the exp label
    cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                              sizeof(unsigned long));
    getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                        numVariables, shift, negBitmaskShifted, offsets
                      );
    insertCritPair( cpTemp, critPairDeg, cpBounds );
  }
  else 
  {
    // we can delete the memory reserved for cpTemp
    omFree( cpTemp );
  }
  omFree(expVecTemp);
  omFree(expVecNewElement);
#if F5EDEBUG
  Print("CRITPAIRINIT-BEGINNING\n");
#endif
}



void criticalPairPrev ( Lpoly* gCurr, const ideal redGB, 
                        const F5Rules& f5Rules, CpairDegBound** cpBounds, 
                        int numVariables, int* shift, unsigned long* negBitmaskShifted, 
                        int* offsets
                      )
{
#if F5EDEBUG
  Print("CRITPAIRPREV-BEGINNING\n");
#endif
  int i, j;
  int* expVecNewElement = (int*) omAlloc((currRing->N+1)*sizeof(int));
  int* expVecTemp       = (int*) omAlloc((currRing->N+1)*sizeof(int));
  pGetExpV(gCurr->p, expVecNewElement); 
  // this must be changed for parallelizing the generation process of critical
  // pairs
  Cpair* cpTemp;
  Cpair* cp         = (Cpair*) omAlloc( sizeof(Cpair) );
  cpTemp            = cp;
  cpTemp->next      = NULL;
  cpTemp->mLabelExp = NULL;
  cpTemp->mLabel1   = NULL;
  cpTemp->smLabel1  = 0;
  cpTemp->mult1     = NULL;
  cpTemp->p1        = gCurr->p;
  cpTemp->rewRule1  = gCurr->rewRule;
  cpTemp->mLabel2   = NULL;
  cpTemp->smLabel2  = 0;
  cpTemp->mult2     = NULL;
  cpTemp->p2        = NULL;

  // we only need to alloc memory for the 1st label as for the initial 
  // critical pairs all 2nd generators have label NULL in F5e
  cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
  cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  int temp;
  long critPairDeg; // degree of the label of the critical pair in the following
  for(i=0; i<IDELEMS(redGB)-1; i++)
  {
    pGetExpV(redGB->m[i], expVecTemp); 
    // computation of the lcm and the corresponding multipliers for the critical
    // pair generated by the new element and elements of the previous iteration
    // steps, i.e. elements already in redGB
    cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = pGetExp(cpTemp->p1, 0); 
    cpTemp->mult2[0]    = pGetExp(redGB->m[i], 0); 
    critPairDeg = 0;
    for(j=1; j<=currRing->N; j++)
    {
      temp  = expVecNewElement[j] - expVecTemp[j];
      if(temp<0)
      {
        cpTemp->mult1[j]    =   -temp;  
        cpTemp->mult2[j]    =   0; 
        cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j] - temp;
        critPairDeg         +=  (cpTemp->rewRule1->label[j] - temp); 
      }
      else
      {
        cpTemp->mult1[j]    =   0;  
        cpTemp->mult2[j]    =   temp;  
        cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j];
        critPairDeg         +=  cpTemp->rewRule1->label[j]; 
      }
    }
    cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
    
    // testing the F5 Criterion
    if( !criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules) &&
        !criterion2(cpTemp->mLabel1, cpTemp->smLabel1, cpTemp->rewRule1)
      )
    {
      // completing the construction of the new critical pair and inserting it
      // to the list of critical pairs 
      cpTemp->p2        = redGB->m[i];
      // now we really need the memory for the exp label
      cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                                sizeof(unsigned long));
      getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                          numVariables, shift, negBitmaskShifted, offsets );
        Print("IN CRITPAIRPREv %ld %ld %ld %ld\n",cpTemp->mLabelExp[1], cpTemp->mLabelExp[2],cpTemp->mLabelExp[3],cpTemp->mLabelExp[4]);
      insertCritPair(cpTemp, critPairDeg, cpBounds);
      Cpair* cp         = (Cpair*) omAlloc( sizeof(Cpair) );
      cpTemp            = cp;
      cpTemp->next      = NULL;
      cpTemp->mLabelExp = NULL;
      cpTemp->mLabel1   = NULL;
      cpTemp->smLabel1  = 0;
      cpTemp->mult1     = NULL;
      cpTemp->p1        = gCurr->p;
      cpTemp->rewRule1  = gCurr->rewRule;
      cpTemp->mLabel2   = NULL;
      cpTemp->smLabel2  = 0;
      cpTemp->mult2     = NULL;
      cpTemp->p2        = NULL;

      cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
      cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
      cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
    }
  }
  // same critical pair processing for the last element in redGB
  // This is outside of the loop to keep memory low, since we know that after
  // this element no new memory for a critical pair must be allocated.
  pGetExpV(redGB->m[IDELEMS(redGB)-1], expVecTemp); 
  // computation of the lcm and the corresponding multipliers for the critical
  // pair generated by the new element and elements of the previous iteration
  // steps, i.e. elements already in redGB
  cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = pGetExp(cpTemp->p1, 0); 
  cpTemp->mult2[0]    = pGetExp(redGB->m[IDELEMS(redGB)-1], 0); 
  critPairDeg = 0;
  for(j=1; j<=currRing->N; j++)
  {
    temp  = expVecNewElement[j] - expVecTemp[j];
    if(temp<0)
    {
      cpTemp->mult1[j]    =   -temp;  
      cpTemp->mult2[j]    =   0; 
      cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j] - temp;
      critPairDeg         +=  (cpTemp->rewRule1->label[j] - temp);
    }
    else
    {
      cpTemp->mult1[j]    =   0;  
      cpTemp->mult2[j]    =   temp;  
      cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j];
      critPairDeg         +=  cpTemp->rewRule1->label[j];
    }
  }
  cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
  
  // testing the F5 Criterion
  if( !criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules) &&
      !criterion2(cpTemp->mLabel1, cpTemp->smLabel1, cpTemp->rewRule1)
    )
  {
    // completing the construction of the new critical pair and inserting it
    // to the list of critical pairs 
    cpTemp->p2        = redGB->m[IDELEMS(redGB)-1];
    // now we really need the memory for the exp label
    cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                              sizeof(unsigned long));
    getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                        numVariables, shift, negBitmaskShifted, offsets
                      );
      Print("IN CRITPAIRPREv %ld %ld %ld %ld\n",cpTemp->mLabelExp[1], cpTemp->mLabelExp[2],cpTemp->mLabelExp[3],cpTemp->mLabelExp[4]);
    insertCritPair(cpTemp, critPairDeg, cpBounds);
  }
  else 
  {
    // we can delete the memory reserved for cpTemp
    omFree( cpTemp );
  }
  omFree(expVecTemp);
  omFree(expVecNewElement);
#if F5EDEBUG
  Print("CRITPAIRPREV-END\n");
#endif
}



void criticalPairCurr ( Lpoly* gCurr, const F5Rules& f5Rules, 
                        CpairDegBound** cpBounds, int numVariables, 
                        int* shift, unsigned long* negBitmaskShifted, int* offsets
                      )
{
#if F5EDEBUG
  Print("CRITPAIRCURR-BEGINNING\n");
#endif
  int i, j;
  unsigned long* mLabelExp;
  bool pairNeeded       = FALSE;
  int* expVecNewElement = (int*) omAlloc((currRing->N+1)*sizeof(int));
  int* expVecTemp       = (int*) omAlloc((currRing->N+1)*sizeof(int));
  pGetExpV(gCurr->p, expVecNewElement); 
  Lpoly* gCurrIter  = gCurr->next;
 Print("HERE %p\n",gCurrIter);
  // this must be changed for parallelizing the generation process of critical
  // pairs
  Cpair* cpTemp;
  Cpair* cp         = (Cpair*) omAlloc( sizeof(Cpair) );
  cpTemp            = cp;
  cpTemp->next      = NULL;
  cpTemp->mLabelExp = NULL;
  cpTemp->mLabel1   = NULL;
  cpTemp->smLabel1  = 0;
  cpTemp->mult1     = NULL;
  cpTemp->p1        = gCurr->p;
  cpTemp->rewRule1  = gCurr->rewRule;
  cpTemp->mLabel2   = NULL;
  cpTemp->smLabel2  = 0;
  cpTemp->mult2     = NULL;

  // we need to alloc memory for the 1st & the 2nd label here
  // both elements are generated during the current iteration step
  cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
      Print("HERE1\n");
  cpTemp->mLabel2   = (int*) omAlloc((currRing->N+1)*sizeof(int));
      Print("HERE2\n");
  cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
  // alloc memory for a temporary (non-integer/SINGULAR internal) exponent vector
  // for fast comparisons at the end which label is greater, those of the 1st or
  // those of the 2nd generator
  // Note: As we do not need the smaller exponent vector we do NOT store both in
  // the critical pair structure, but only the greater one. Thus the following
  // memory is freed before the end of criticalPairCurr()
  Print("dddd\n");
  unsigned long* checkExp = (unsigned long*) omAlloc0(NUMVARS*sizeof(unsigned long));
  int temp;
  long critPairDeg; // degree of the label of the pair in the following
  //pWrite( gCurrIter->p );
  while(gCurrIter->next)
  {
    cpTemp->p2        = gCurrIter->p;
    Print("2nd generator of pair: ");
    //pWrite( cpTemp->p2 );
    cpTemp->rewRule2  = gCurrIter->rewRule;
    pGetExpV(gCurrIter->p, expVecTemp); 
    // computation of the lcm and the corresponding multipliers for the critical
    // pair generated by the new element and elements of the previous iteration
    // steps, i.e. elements already in redGB
    cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = pGetExp(cpTemp->p1, 0); 
    cpTemp->mLabel2[0]  = cpTemp->mult2[0]  = pGetExp(cpTemp->p2, 0); 
    critPairDeg = 0;
    for(j=1; j<currRing->N+1; j++)
    {
      temp  = expVecNewElement[j] - expVecTemp[j];
      if(temp<0)
      {
        pairNeeded          =   TRUE;
        cpTemp->mult1[j]    =   -temp;  
        cpTemp->mult2[j]    =   0; 
        cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j] - temp;
        cpTemp->mLabel2[j]  =   cpTemp->rewRule2->label[j];
        critPairDeg         +=  cpTemp->rewRule1->label[j] - temp; 
        Print("PAIRDEG1: %d\n",critPairDeg);
      }
      else
      {
        cpTemp->mult1[j]    =   0;  
        cpTemp->mult2[j]    =   temp;  
        cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j];
        cpTemp->mLabel2[j]  =   cpTemp->rewRule2->label[j] + temp;
        critPairDeg         +=  cpTemp->rewRule1->label[j]; 
        Print("PAIRDEG2: %d\n",critPairDeg);
      }
    }
    // compute only if there is a "real" multiple of p1 needed
    // otherwise we can go on with the next element, since the 
    // 2nd generator is a past reducer of p1 which was not allowed
    // to reduce!
    if( pairNeeded )
    {
      cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
      cpTemp->smLabel2 = ~getShortExpVecFromArray(cpTemp->mLabel2);
      
      // testing the F5 & Rewritten Criterion
      if( !criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules) 
          && !criterion1(cpTemp->mLabel2, cpTemp->smLabel2, &f5Rules) 
          && !criterion2(cpTemp->mLabel1, cpTemp->smLabel1, cpTemp->rewRule1)
          && !criterion2(cpTemp->mLabel2, cpTemp->smLabel2, cpTemp->rewRule2)
        ) 
      {
        // completing the construction of the new critical pair and inserting it
        // to the list of critical pairs 
        // now we really need the memory for the exp label
        cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                                  sizeof(unsigned long));
        getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                            numVariables, shift, negBitmaskShifted, offsets
                          );
        getExpFromIntArray( cpTemp->mLabel2, checkExp, numVariables,
                            shift, negBitmaskShifted, offsets
                          );
        Print("IN CRITPAIRCURR %ld %ld %ld %ld\n",cpTemp->mLabelExp[1], cpTemp->mLabelExp[2],cpTemp->mLabelExp[3],cpTemp->mLabelExp[4]);
        // compare which label is greater and possibly switch the 1st and 2nd 
        // generator in cpTemp
        // exchange generator 1 and 2 in cpTemp
        if( expCmp(cpTemp->mLabelExp, checkExp) == -1 )
        {
          poly pTempHolder                = cpTemp->p1;
          int* mLabelTempHolder           = cpTemp->mLabel1;
          int* multTempHolder             = cpTemp->mult1;
          unsigned long smLabelTempHolder = cpTemp->smLabel1;  
          RewRules* rewRuleTempHolder     = cpTemp->rewRule1;
          unsigned long* expTempHolder    = cpTemp->mLabelExp;

          cpTemp->p1                      = cpTemp->p2;
          cpTemp->p2                      = pTempHolder;
          cpTemp->mLabel1                 = cpTemp->mLabel2;
          cpTemp->mLabel2                 = mLabelTempHolder;
          cpTemp->mult1                   = cpTemp->mult2;
          cpTemp->mult2                   = multTempHolder;
          cpTemp->smLabel1                = cpTemp->smLabel2;
          cpTemp->smLabel2                = smLabelTempHolder;
          cpTemp->rewRule1                = cpTemp->rewRule2;
          cpTemp->rewRule2                = rewRuleTempHolder;
          cpTemp->mLabelExp               = checkExp;
          checkExp                        = expTempHolder;
        }
        
        insertCritPair(cpTemp, critPairDeg, cpBounds);
      Print("HERE\n");
        
        Cpair* cp         = (Cpair*) omAlloc( sizeof(Cpair) );
        cpTemp            = cp;
        cpTemp->next      = NULL;
        cpTemp->mLabelExp = NULL;
        cpTemp->mLabel1   = NULL;
        cpTemp->smLabel1  = 0;
        cpTemp->mult1     = NULL;
        cpTemp->p1        = gCurr->p;
        cpTemp->rewRule1  = gCurr->rewRule;
        cpTemp->mLabel2   = NULL;
        cpTemp->smLabel2  = 0;
        cpTemp->mult2     = NULL;
        cpTemp->rewRule2  = (gCurrIter->next)->rewRule;
        cpTemp->mLabel1   = (int*) omAlloc((currRing->N+1)*sizeof(int));
        cpTemp->mLabel2   = (int*) omAlloc((currRing->N+1)*sizeof(int));
        cpTemp->mult1     = (int*) omAlloc((currRing->N+1)*sizeof(int));
        cpTemp->mult2     = (int*) omAlloc((currRing->N+1)*sizeof(int));
      }
    }
    pairNeeded  = FALSE;
    gCurrIter   = gCurrIter->next;
  }
  Print("COMPUTATION OF LAST POSSIBLE PAIR IN CRITPAIRCURR\n");
  // same critical pair processing for the last element in gCurr
  // This is outside of the loop to keep memory low, since we know that after
  // this element no new memory for a critical pair must be allocated.
  cpTemp->p2  = gCurrIter->p;
  Print("2nd generator of pair: ");
  pGetExpV(gCurrIter->p, expVecTemp); 
  // computation of the lcm and the corresponding multipliers for the critical
  // pair generated by the new element and elements of the previous iteration
  // steps, i.e. elements already in redGB
  cpTemp->mLabel1[0]  = cpTemp->mult1[0]  = pGetExp(cpTemp->p1, 0); 
    Print("AT THIS PLACE2: %ld\n",pGetExp(gCurrIter->p,0));
    //pWrite(gCurrIter->p);
  cpTemp->rewRule2  = gCurrIter->rewRule;
  cpTemp->mLabel2[0]  = cpTemp->mult2[0]  = pGetExp(gCurrIter->p, 0); 
  critPairDeg = 0;
  for(j=1; j<=currRing->N; j++)
  {
    temp  = expVecNewElement[j] - expVecTemp[j];
    if(temp<0)
    {
      Print("HERE DRIN %d\n",j);
      pairNeeded          =   TRUE;
      cpTemp->mult1[j]    =   -temp;  
      cpTemp->mult2[j]    =   0; 
      cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j] - temp;
      cpTemp->mLabel2[j]  =   cpTemp->rewRule2->label[j];
      critPairDeg         +=  cpTemp->rewRule1->label[j] - temp;
      Print("CRITDEG: %d -- %d drauf\n", critPairDeg, cpTemp->mLabel1[j]);
    }
    else
    {
      cpTemp->mult1[j]    =   0;  
      cpTemp->mult2[j]    =   temp;  
      cpTemp->mLabel1[j]  =   cpTemp->rewRule1->label[j];
      cpTemp->mLabel2[j]  =   cpTemp->rewRule2->label[j] + temp;
      critPairDeg         +=  cpTemp->rewRule1->label[j];
      Print("CRITDEG: %d -- %d drauf\n", critPairDeg, cpTemp->mLabel1[j]);
    }
  }
  if( pairNeeded )
  {
    cpTemp->smLabel1 = ~getShortExpVecFromArray(cpTemp->mLabel1);
    cpTemp->smLabel2 = ~getShortExpVecFromArray(cpTemp->mLabel2);
    
    // testing the F5 Criterion
    if( !criterion1(cpTemp->mLabel1, cpTemp->smLabel1, &f5Rules) 
        && !criterion1(cpTemp->mLabel2, cpTemp->smLabel2, &f5Rules) 
        && !criterion2(cpTemp->mLabel1, cpTemp->smLabel1, cpTemp->rewRule1)
        && !criterion2(cpTemp->mLabel2, cpTemp->smLabel2, cpTemp->rewRule2)
      ) 
    {
      // completing the construction of the new critical pair and inserting it
      // to the list of critical pairs 
      // now we really need the memory for the exp label
      cpTemp->mLabelExp = (unsigned long*) omAlloc0(NUMVARS*
                                sizeof(unsigned long));
      getExpFromIntArray( cpTemp->mLabel1, cpTemp->mLabelExp, 
                          numVariables, shift, negBitmaskShifted, offsets
                        );
      getExpFromIntArray( cpTemp->mLabel2, checkExp, numVariables,
                          shift, negBitmaskShifted, offsets
                        );
        Print("IN CRITPAIRCURR %ld %ld %ld %ld\n",cpTemp->mLabelExp[1], cpTemp->mLabelExp[2],cpTemp->mLabelExp[3],cpTemp->mLabelExp[4]);
      // compare which label is greater and possibly switch the 1st and 2nd 
      // generator in cpTemp
      // exchange generator 1 and 2 in cpTemp
      if( expCmp(cpTemp->mLabelExp, checkExp) == -1 )
      {
        poly pTempHolder                = cpTemp->p1;
        int* mLabelTempHolder           = cpTemp->mLabel1;
        int* multTempHolder             = cpTemp->mult1;
        unsigned long smLabelTempHolder = cpTemp->smLabel1;  
        RewRules* rewRuleTempHolder     = cpTemp->rewRule1;
        unsigned long* expTempHolder    = cpTemp->mLabelExp;

        cpTemp->p1                      = cpTemp->p2;
        cpTemp->p2                      = pTempHolder;
        cpTemp->mLabel1                 = cpTemp->mLabel2;
        cpTemp->mLabel2                 = mLabelTempHolder;
        cpTemp->mult1                   = cpTemp->mult2;
        cpTemp->mult2                   = multTempHolder;
        cpTemp->smLabel1                = cpTemp->smLabel2;
        cpTemp->smLabel2                = smLabelTempHolder;
        cpTemp->rewRule1                = cpTemp->rewRule2;
        cpTemp->rewRule2                = rewRuleTempHolder;
        cpTemp->mLabelExp               = checkExp;
        checkExp                        = expTempHolder;
      }
      
      insertCritPair(cpTemp, critPairDeg, cpBounds);
      Print("HERE\n");
    } 
    else 
    {
      // we can delete the memory reserved for cpTemp
      omFree( cpTemp );
    }
  }
  omFree(checkExp);
  omFree(expVecTemp);
  omFree(expVecNewElement);
#if F5EDEBUG
  Print("CRITPAIRCURR-END\n");
#endif
}



void insertCritPair( Cpair* cp, long deg, CpairDegBound** bound )
{
  Cpair* tempForDel = NULL;
#if F5EDEBUG
  Print("INSERTCRITPAIR-BEGINNING deg bound %p\n",*bound);
  Print("ADDRESS NEW CRITPAIR: %p -- degree %ld\n",cp, deg);
  //pWrite(cp->p1);
  //pWrite(cp->p2);
  if( (*bound) ) 
  {
    Print("ADDRESS BOUND CRITPAIR: %p -- deg %ld -- length %d\n",(*bound)->cp,(*bound)->deg, (*bound)->length);
    //pWrite( (*bound)->cp->p1 );
  }
#endif
  if( !(*bound) ) // empty list?
  {
    CpairDegBound* boundNew = (CpairDegBound*) omAlloc(sizeof(CpairDegBound));
    boundNew->next          = NULL;
    boundNew->deg           = deg;
    boundNew->cp            = cp;
    boundNew->length        = 1;
    *bound                  = boundNew;
    Print("here1\n");
  }
  else
  {
    if((*bound)->deg < deg) 
    {
      CpairDegBound* temp = *bound;
      while((temp)->next && ((temp)->next->deg < deg))
      {
        temp = (temp)->next;
      }
      if( (temp)->next && (temp)->next->deg == deg )
      {
        cp->next          = (temp)->next->cp;
        (temp)->next->cp  = cp;
        (temp)->next->length++;
        // if there exist other elements in the list with the very same label
        // then delete them as they will be detected by the Rewritten Criterion
        // of F5 nevertheless
        tempForDel  = cp;
        while( tempForDel->next )
        {
          if( expCmp(cp->mLabelExp,(tempForDel->next)->mLabelExp) == 0 )
          {
            Cpair* tempDel    = tempForDel->next;
            tempForDel->next  = (tempForDel->next)->next;
            omFree( tempDel );
            (temp)->next->length--;
          }
          // tempForDel->next could be NULL as we have deleted one element
          // inbetween
          if( tempForDel->next )
          {
            tempForDel  = tempForDel->next;
          }
        }
      }
      else
      {
        CpairDegBound* boundNew = (CpairDegBound*) omAlloc(sizeof(CpairDegBound));
        boundNew->next          = (temp)->next;
        boundNew->deg           = deg;
        boundNew->cp            = cp;
        boundNew->length        = 1;
        (temp)->next           = boundNew;
        Print("HERE %p -- %p\n",temp,*bound);
      }
    }
    else
    {
      if((*bound)->deg == deg) 
      {
        cp->next      = (*bound)->cp;
        (*bound)->cp  = cp;
        (*bound)->length++;
        // if there exist other elements in the list with the very same label
        // then delete them as they will be detected by the Rewritten Criterion
        // of F5 nevertheless
        tempForDel    = cp;
        while( tempForDel->next )
        {
          Print("%p -- NEXT CP ADDRESS %p\n",tempForDel,tempForDel->next);
          Print("COMPINDEX: %ld -- %ld\n",tempForDel->next->mLabelExp[currRing->pCompIndex],cp->mLabelExp[currRing->pCompIndex]);
          Print("%d %d %d %d %d\n",tempForDel->next->mLabel1[0],tempForDel->next->mLabel1[1], tempForDel->next->mLabel1[2],tempForDel->next->mLabel1[3],tempForDel->next->mLabel1[4]);
          Print("%d %d %d %d %d\n",cp->mLabel1[0],cp->mLabel1[1], cp->mLabel1[2],cp->mLabel1[3],cp->mLabel1[4]);
          Print("%ld %ld %ld %ld\n",tempForDel->next->mLabelExp[1], tempForDel->next->mLabelExp[2],tempForDel->next->mLabelExp[3],tempForDel->next->mLabelExp[4]);
          Print("%ld %ld %ld %ld\n",cp->mLabelExp[1], cp->mLabelExp[2],cp->mLabelExp[3],cp->mLabelExp[4]);
          if( expCmp(cp->mLabelExp,(tempForDel->next)->mLabelExp) == 0 )
          {
            Print("here3\n");
            Cpair* tempDel    = tempForDel->next;
            tempForDel->next  = (tempForDel->next)->next;
            omFree( tempDel );
            (*bound)->length--;
          }
          // tempForDel->next could be NULL as we have deleted one element
          // inbetween
          if( tempForDel->next )
          {
            tempForDel  = tempForDel->next;
          }
        }
      }
      else
        {
        CpairDegBound* boundNew = (CpairDegBound*) omAlloc(sizeof(CpairDegBound));
        boundNew->next          = *bound;
        boundNew->deg           = deg;
        boundNew->cp            = cp;
        boundNew->length        = 1;
        *bound                  = boundNew;
      }
    }
  }
#if F5EDEBUG
  //pWrite( (*bound)->cp->p2 );
  CpairDegBound* test = *bound;
  Print("-------------------------------------\n");
  while( test )
  {
    Print("DEGREE %d\n",test->deg);
    Cpair* testcp = test->cp;
    while( testcp )
    {
      Print("Pairs: %p\n",testcp);
      pWrite(pHead(testcp->p1));
      pWrite(pHead(testcp->p2));
      testcp = testcp->next;
    }
    test  = test->next;
  }
  Print("-------------------------------------\n");
  Print("INSERTCRITPAIR-END deg bound %p\n",*bound);
#endif
}



inline BOOLEAN criterion1 ( const int* mLabel, const unsigned long smLabel, 
                            const F5Rules* f5Rules
                          )
{
  int i = 0;
  int j = currRing->N;
#if F5EDEBUG
    Print("CRITERION1-BEGINNING\nTested Element: ");
    while( j )
    {
      Print("%d ",mLabel[(currRing->N)-j]);
      j--;
    }
    j = currRing->N;
    Print("\n %ld\n",smLabel);
    
#endif
  nextElement:
  for( ; i < f5Rules->size; i++)
  {
#if F5EDEBUG
    Print("F5 Rule: ");
    while( j )
    {
      Print("%d ",f5Rules->label[i][(currRing->N)-j]);
      j--;
    }
    j = currRing->N;
    Print("\n %ld\n",f5Rules->slabel[i]);
#endif
    if(!(smLabel & f5Rules->slabel[i]))
    {
      while(j)
      {
        if(mLabel[j] < f5Rules->label[i][j])
        {
          j = currRing->N;
          i++;
          goto nextElement;
        }
        j--;
      }
#if F5EDEBUG
        Print("CRITERION1-END-DETECTED \n");
#endif
      return TRUE;
    }
      Print("HERE\n");

  }
#if F5EDEBUG
  Print("CRITERION1-END \n");
#endif
  return FALSE;
}



inline BOOLEAN criterion2 ( const int* mLabel, const unsigned long smLabel, 
                            RewRules* rewRules
                          )
{
#if F5EDEBUG
    Print("CRITERION2-BEGINNING %p\n", rewRules);
    int counter = 0;
#endif
  int j = currRing->N;
  if( rewRules->next )
  {
    RewRules* temp = rewRules->next;
    Print("%p\n", temp);
#if F5EDEBUG
    poly p = pOne();
    pSetExp(p,2,1);
    pWrite(p);
    Print("SHORTEXP %ld\n", pGetShortExpVector(p) );
    Print("Tested Element: %ld\n",smLabel);
    while( j )
    {
      Print("%d ",mLabel[(currRing->N)-j]);
      j--;
    }
    j = currRing->N;
    Print("\n");
#endif
    nextElement:
    while( NULL != temp )
    {
#if F5EDEBUG
    pSetExp(p,4,1);
    pWrite(p);
    Print("SHORTEXP %ld -- %ld\n", pGetShortExpVector(p), temp->slabel );
    counter++;
    while( j )
    {
      Print("%d ",temp->label[(currRing->N)-j]);
      j--;
    }
    j = currRing->N;
    Print("\n");
#endif
      if(!(smLabel & temp->slabel))
      {
        while(j)
        {
          if(mLabel[j] < temp->label[j])
          {
            j     = currRing->N;
            temp  = temp->next;
            goto nextElement;
          }
          j--;
        }
#if F5EDEBUG
        Print("CRITERION2-END-DETECTED \n");
#endif
          return TRUE;
      }
      Print("HERE\n");
      temp  = temp->next;
    }
  }
#if F5EDEBUG
  Print("ELEMENTS IN LIST %d\n", counter );
  Print("CRITERION2-END \n");
#endif
  return FALSE;
}



void computeSpols ( kStrategy strat, CpairDegBound* cp, ideal redGB, Lpoly** gCurr, 
                    const F5Rules* f5Rules, int numVariables, 
                    int* shift, unsigned long* negBitmaskShifted, int* offsets
                  )
{
#if F5EDEBUG
  Print("COMPUTESPOLS-BEGINNING\n");
#endif
  // check whether a new deg step starts or not
  // needed to keep the synchronization of RewRules list and Spoly list alive
  BOOLEAN newStep         = TRUE; 
  Cpair* temp             = NULL;
  Cpair* tempDel          = NULL;
  RewRules* rewRulesLast  = NULL;
  // if a higher label reduction happens we need
  // to store rule corresponding to the current computed
  // polynomial to be able to combine them after finishing
  // the reduction process 
  RewRules* rewRulesCurr  = NULL; 
  Spoly* spolysLast       = NULL;
  Spoly* spolysFirst      = NULL;
  // start the rewriter rules list with a NULL element for the recent,
  // i.e. initial element in \c gCurr
  rewRulesLast            = (*gCurr)->rewRule;
  // this will go on for the complete current iteration step!
  // => after computeSpols() terminates this iteration step is done!
  int* multTemp           = (int*) omAlloc0( (currRing->N+1)*sizeof(int) );
  int* multLabelTemp      = (int*) omAlloc0( (currRing->N+1)*sizeof(int) );
  poly sp;
  while( cp )
  {
#if F5EDEBUG
    Print("START OF NEW DEG ROUND: CP %ld | %p -- %p: # %d\n",cp->deg,cp->cp,cp,cp->length);
    Print("NEW CPDEG? %p\n",cp->next);
#endif
    temp  = sort(cp->cp, cp->length); 
    CpairDegBound* cpDel  = cp;
    cp                    = cp->next;
    omFree( cpDel );
    // compute all s-polynomials of this degree step and reduce them
    // w.r.t. redGB as preparation for the current reduction steps 
    while( NULL != temp )
    {
      Print("Last Element in Rewrules? %p points to %p\n", rewRulesLast,
              rewRulesLast->next);
      Print("COMPUTED PAIR: %p -- NEXT PAIR %p\n",temp, temp->next);
      // if the pair is not rewritable add the corresponding rule and 
      // and compute the corresponding s-polynomial (and pre-reduce it
      // w.r.t. redGB
      if( !criterion2(temp->mLabel1, temp->smLabel1, temp->rewRule1) ||
          (temp->mLabel2 && !criterion2(temp->mLabel1, temp->smLabel1, temp->rewRule1)) 
        )
      {
        Print("HERE RULES\n");
        RewRules* newRule   = (RewRules*) omAlloc( sizeof(RewRules) );
        newRule->next       = NULL;
        newRule->label      = temp->mLabel1;
        newRule->slabel     = ~temp->smLabel1;
        rewRulesLast->next  = newRule;
        rewRulesLast        = newRule; 
        if( newStep )
        {
          rewRulesCurr  = newRule; 
          newStep       = FALSE;
        }
#if F5EDEBUG
        Print("Last Element in Rewrules? %p points to %p\n", rewRulesLast,rewRulesLast->next);
#endif

        // from this point on, rewRulesLast != NULL, thus we do not need to test this
        // again in the following iteration over the list of critical pairs
        
  //#if F5EDEBUG
        Print("CRITICAL PAIR BEFORE S-SPOLY COMPUTATION:\n");
        Print("%p\n",temp);
        Print("GEN1: %p\n",temp->p1);
        pWrite(pHead(temp->p1));
        pTest(temp->p1);
        Print("GEN2: %p\n",temp->p2);
        pWrite(pHead(temp->p2));
        pTest(temp->p2);
        int ctr = 0;
        for( ctr=0; ctr<=currRing->N; ctr++ )
        {
          Print("%d ",temp->mLabel1[ctr]);
        }
        Print("\n");
  //#endif

        // check if the critical pair is a trivial one, i.e. a pair generated 
        // during a higher label reduction 
        // => p1 is already the s-polynomial reduced w.r.t. redGB and we do not 
        // need to reduce and/or compute anything
        if( temp->p2 )
        { 
          // compute s-polynomial and reduce it w.r.t. redGB
          sp  = reduceByRedGBCritPair ( temp, strat, numVariables, shift, 
                                        negBitmaskShifted, offsets 
                                      );
        }
        else
        {
          sp = temp->p1;
        }
#if F5EDEBUG
        Print("BEFORE:  ");
        pWrite( sp );
        pTest(sp);
      Print("TEMPSBEFORE OK? %p -- %p\n",temp,temp->next);
#endif
        if( sp )
        {
          // store the s-polynomial in the linked list for further
          // reductions in currReduction()
      Print("HERE\n");
          pNorm( sp ); 
          Spoly* newSpoly     = (Spoly*) omAlloc( sizeof(struct Spoly) );
          newSpoly->p         = sp;
          newSpoly->labelExp  = temp->mLabelExp;
          newSpoly->next      = NULL;
          Print("ALLOC MEMORY: %p -- %p\n", newSpoly, newSpoly->p);
          if( NULL == spolysFirst )
          {
            spolysFirst  = newSpoly;
            spolysLast   = newSpoly;
          }
          else
          {
            spolysLast->next  = newSpoly;
            spolysLast        = newSpoly;
          }
      Print("HERE1\n");
        }
        else // sp == 0
        {
      Print("HERE1\n");
          pDelete( &sp );
        }
      }
      tempDel  = temp;
      temp     = temp->next;
      // note that this has no influence on the address of temp->label!
      // its memory was allocated at a different place and we still need these:
      // those are just the rewrite rules for the newly computed elements!
      omFree(tempDel);
    }
    // all rules and s-polynomials of this degree step are computed and 
    // prepared for further reductions in currReduction()
    Print("REWRULES BEFORE CURRRED: %p -- %p\n", rewRulesCurr, rewRulesLast);
    currReduction ( strat, spolysFirst, spolysLast, rewRulesCurr, &rewRulesLast, 
                    redGB, &cp, gCurr, f5Rules, multTemp, multLabelTemp, 
                    numVariables, shift, negBitmaskShifted, offsets
                  );
    // elements are added to linked list gCurr => start next degree step
    spolysFirst = spolysLast  = NULL;
    Print("REWRULES AFTER CURRRED: %p -- %p\n", rewRulesCurr, rewRulesLast);
    newStep     = TRUE; 
#if F5EDEBUG
  Print("DEGREE STEP DONE:\n------\n");
  Lpoly* gCurrTemp = *gCurr;
  while( gCurrTemp )
  {
    pWrite( pHead(gCurrTemp->p) );
    gCurrTemp = gCurrTemp->next;
  }
  Print("-------\n");
#endif
  }
  // get back the new list of elements in gCurr, i.e. the list of elements
  // computed in this iteration step
  omFree( multTemp );
#if F5EDEBUG
  Print("ITERATION STEP DONE: \n");
  Lpoly* gCurrTemp = *gCurr;
  while( gCurrTemp )
  {
    pWrite( pHead(gCurrTemp->p) );
    gCurrTemp = gCurrTemp->next;
  }
  Print("COMPUTESPOLS-END\n");
#endif
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
// here should computeSpols stop
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////



inline void kBucketCopyToPoly(kBucket_pt bucket, poly *p, int *length)
{
  int i = kBucketCanonicalize(bucket);
  if (i > 0)
  {
  #ifdef USE_COEF_BUCKETS
    MULTIPLY_BUCKET(bucket,i);
    //bucket->coef[i]=NULL;
  #endif
    *p = pCopy(bucket->buckets[i]);
    *length = bucket->buckets_length[i];
  }
  else
  {
    *p = NULL;
    *length = 0;
  }
}



void currReduction  ( 
                      kStrategy strat, Spoly* spolysFirst, Spoly* spolysLast, 
                      RewRules* rewRulesCurr, RewRules** rewRulesLast,
                      ideal redGB, CpairDegBound** cp, Lpoly** gCurrFirst, 
                      const F5Rules* f5Rules, int* multTemp, 
                      int* multLabelTemp, int numVariables, int* shift, 
                      unsigned long* negBitmaskShifted, int* offsets
                    )

{
#if F5EDEBUG
    Print("CURRREDUCTION-BEGINNING: GCURR %p -- %p\n",*gCurrFirst, (*gCurrFirst)->next);
#endif
  BOOLEAN isMult    = FALSE;
  // check needed to ensure termination of F5 (see F5+)
  // this will be added to all new labeled polynomials to check
  // when to terminate the algorithm
  BOOLEAN redundant = FALSE;
  int i;
  unsigned long multLabelShortExp;
  static int tempLength           = 0;
  unsigned short int canonicalize = 0; 
  Spoly* spTemp                   = spolysFirst;
  kBucket* bucket                 = kBucketCreate();
  Lpoly* temp;
  unsigned long bucketExp;
#if F5EDEBUG
  Print("LIST OF SPOLYS TO BE REDUCED: \n---------------\n");
  while( spTemp )
  {
    Print("%p -- ",spTemp->p);
    pWrite( pHead(spTemp->p) );
    spTemp = spTemp->next;
  }
  Print("---------------\n");
  spTemp = spolysFirst;
#endif
  // iterate over all elements in the s-polynomial list
  while( NULL != spTemp )
  { 
    Print("DABEI?\n");
    Print("SPTEMP TO BE REDUCED: %p : %p -- ", spTemp, spTemp->p );
    Print("SPTEMP NEXT?: %p\n", spTemp->next );
    Print("DABEI?\n");
    pWrite( pHead(spTemp->p) );
    kBucketInit( bucket, spTemp->p, pLength(spTemp->p) );
    temp  = *gCurrFirst;
    //----------------------------------------
    // reduction of the leading monomial of sp
    //----------------------------------------
    // Note that we need to make this top reduction explicit to be able to decide
    // if the returned polynomial is redundant or not!
    // search for reducers in the list gCurr
    redundant  = FALSE;
    while ( temp )
    {
      startagainTop:
      bucketExp = ~( pGetShortExpVector(kBucketGetLm(bucket)) );
      Print("POSSIBLE REDUCER %p ",temp);
      Print("%p\n",temp->p);
      pTest( temp->p );
      pWrite(pHead(temp->p));
      if( isDivisibleGetMult( temp->p, temp->sExp, kBucketGetLm( bucket ), 
                              bucketExp, &multTemp, &isMult
                            ) 
        )
      {
        redundant = TRUE;
        // if isMult => lm(sp) > lm(temp->p) => we need to multiply temp->p by 
        // multTemp and check this multiple by both criteria
        Print("ISMULT %d\n",isMult);
        if( isMult )
        {
          // compute the multiple of the rule of temp & multTemp
          for( i=1;i<(currRing->N)+1; i++ )
          {
            multLabelTemp[i]  = multTemp[i] + temp->rewRule->label[i];
          }
          multLabelTemp[0]    = temp->rewRule->label[0];
          multLabelShortExp   = ~getShortExpVecFromArray( multLabelTemp );
          
          // test the multiplied label by both criteria 
          if( !criterion1( multLabelTemp, multLabelShortExp, f5Rules ) && 
              !criterion2( multLabelTemp, multLabelShortExp, temp->rewRule )
            )
          { 
            static unsigned long* multTempExp = (unsigned long*) 
                            omAlloc0( NUMVARS*sizeof(unsigned long) );
            unsigned long* multLabelTempExp = (unsigned long*) 
                            omAlloc0( NUMVARS*sizeof(unsigned long) );
            getExpFromIntArray( multLabelTemp, multLabelTempExp, numVariables,
                                shift, negBitmaskShifted, offsets
                              );   
            // if a higher label reduction takes place we need to create
            // a new Lpoly with the higher label and store it in a different 
            // linked list for later reductions

            if( expCmp( multLabelTempExp, spTemp->labelExp ) == 1 )
            {            
//#if F5EDEBUG
            Print("HIGHER LABEL REDUCTION \n");
//#endif
              poly newPoly  = pInit();
              int length;
              int cano            = kBucketCanonicalize( bucket );
              kBucket* newBucket  = kBucketCreate();
              kBucketInit ( newBucket, pCopy(bucket->buckets[cano]), 
                            bucket->buckets_length[cano] 
                          );
              Print("NEW POLYNOMIAL FROM BUCKET: ");
              //pWrite(kBucketGetLm(newBucket));
              Print("HERE?\n");
              //pWrite(kBucketGetLm(newBucket));
              //multiplier->exp       = multTempExp;
              //getExpFromIntArray( multTemp, multiplier->exp, numVariables, shift, 
              //                    negBitmaskShifted, offsets
              //                  );

              static poly multiplier = pOne();
              getExpFromIntArray( multTemp, multiplier->exp, numVariables, shift, 
                                  negBitmaskShifted, offsets
                                );
              // throw away the leading monomials of reducer and bucket
              pSetm( multiplier );
              p_SetCoeff( multiplier, pGetCoeff(kBucketGetLm(newBucket)), currRing );
              tempLength = pLength( temp->p->next );
              kBucketExtractLm(newBucket);
              Print("MULTIPLIER: ");
              pWrite(multiplier);
              Print("REDUCER: ");
              pWrite( pHead(temp->p) );
              kBucket_Minus_m_Mult_p( newBucket, multiplier, temp->p->next, 
                                      &tempLength 
                                    ); 
              // get monomials out of bucket and save the poly as first generator 
              // of a new critical pair
              int length2 = 0;
              //pWrite( kBucketGetLm(newBucket) );
              kBucketClear( newBucket, &newPoly, &length2 );
              Print("NEWPOLY: ");
              //pWrite( newPoly );
              newPoly = reduceByRedGBPoly( newPoly, strat );
              
              // add the new rule even when newPoly is zero!
              RewRules* newRule   = (RewRules*) omAlloc( sizeof(RewRules) );
              newRule->next       = NULL;
              newRule->label = (int*) omAlloc( (currRing->N+1) * sizeof(int) );
              int j = 0;
              for( j=0;j<currRing->N+1;j++ )
              {
                newRule->label[j] = multLabelTemp[j];
              }
              newRule->slabel       = multLabelShortExp;
              // insert the new element at the right position, i.e.
              // ordered w.r.t. the corresponding rewrite rule
              RewRules* tempRew     = rewRulesCurr;
              Spoly* tempSpoly      = spTemp;
              while ( NULL != tempSpoly->next && 
                      expCmp( multLabelTempExp, tempSpoly->next->labelExp ) == 1 
                    )
              {
                tempSpoly = tempSpoly->next;
                tempRew   = tempRew->next;
              }
              newRule->next = tempRew->next;
              tempRew->next = newRule;
              if( NULL == tempRew->next )
              {
                (*rewRulesLast) = newRule; 
              }
              
              Print("NEWLY AFTER REDGB REDUCTION: %p\n",newPoly);
              pWrite( pHead(newPoly) );
              
              if( NULL != newPoly )
              {
                pNorm( newPoly );
              }
              // even if newPoly = 0 we need to add it to the list of s-polynomials
              // to keep it with the list of rew rules synchronized!
              Spoly* spNew      = (Spoly*) omAlloc( sizeof(struct Spoly) );
              spNew->p          = newPoly;
              spNew->labelExp   = multLabelTempExp;
              spNew->next       = tempSpoly->next;
              tempSpoly->next   = spNew;
#if F5EDEBUG
  Print("ADDED TO LIST OF SPOLYS TO BE REDUCED: \n---------------\n");
    Print("%p -- ",spolysLast);
    pWrite( pHead(newPoly) );
  Print("---------------\n");
#endif
              
              // get back on track for the old poly which has to be checked for 
              // reductions by the following element in gCurr
              Print("Poly copying: ");
              //pWrite(kBucketGetLm( bucket ) );
              isMult    = FALSE;
              redundant = TRUE;
              temp      = temp->next;
              goto startagainTop;
            }
            // else we can go on and reduce sp
            // The multiplied reducer will be reduced w.r.t. strat before the 
            // bucket reduction starts!
            static poly multiplier = pOne();
            static poly multReducer;
            getExpFromIntArray( multTemp, multiplier->exp, numVariables, shift, 
                                negBitmaskShifted, offsets
                              );
            // throw away the leading monomials of reducer and bucket
            pSetm( multiplier );
            p_SetCoeff( multiplier, pGetCoeff(kBucketGetLm(bucket)), currRing );
            kBucketExtractLm(bucket);
            // build the multiplied reducer (note that we do not need the leading
            // term at all!
            Print("LETS SEE -----------------------\n");
            Print("MULT: %p\n", multiplier );
            //pWrite( multiplier );
            //pWrite( temp->p->next );
            multReducer = pp_Mult_mm( temp->p->next, multiplier, currRing );
            Print("MULTRED: %p\n", *multReducer );
            pWrite( pHead(multReducer) );
            multReducer = reduceByRedGBPoly( multReducer, strat );
            //  length must be computed after the reduction with 
            //  redGB!
            tempLength = pLength( multReducer );
            
            Print("AFTER REDUCTION STEP: ");
            //pWrite( multReducer );
            //pWrite( temp->p );
            kBucket_Add_q( bucket, pNeg(multReducer), &tempLength ); 
            pWrite( kBucketGetLm(bucket) );
            //pDelete( &multReducer );
            if( canonicalize++ % 40 )
            {
              kBucketCanonicalize( bucket );
              canonicalize = 0;
            }
            isMult    = FALSE;
            redundant = FALSE;
            if( kBucketGetLm( bucket ) )
            {
              temp  = *gCurrFirst;
            }
            else
            {
              break;
            }
            goto startagainTop;
          }
        }
        // isMult = 0 => multTemp = 1 => we do not need to test temp->p by any
        // criterion again => go on with reduction steps
        else
        {
          //pWrite(kBucketGetLm(bucket));
          number coeff  = pGetCoeff(kBucketGetLm(bucket));
          static poly tempNeg  = pInit();
          // throw away the leading monomials of reducer and bucket
          tempNeg       = pCopy( temp->p );
          tempLength    = pLength( tempNeg->next );
          Print("HERE\n");
          //pWrite(kBucketGetLm(bucket));
          p_Mult_nn( tempNeg, coeff, currRing );
          pSetm( tempNeg );
          kBucketExtractLm(bucket);
            Print("REDUCTION WITH: ");
            pWrite( temp->p );
          kBucket_Add_q( bucket, pNeg(tempNeg->next), &tempLength ); 
          Print("AFTER REDUCTION STEP: ");
          pWrite( kBucketGetLm(bucket) );
          if( canonicalize++ % 40 )
          {
            kBucketCanonicalize( bucket );
            canonicalize = 0;
          }
          //pDelete( &tempNeg );
          redundant = FALSE;
          if( kBucketGetLm( bucket ) )
          {
            temp  = *gCurrFirst;
          }
          else
          {
            break;
          }
          goto startagainTop;
        } 
      }
      temp  = temp->next;
    }
    // we know that sp = 0 at this point!
    //pWrite(kBucketGetLm(bucket));
    spTemp->p  = kBucketExtractLm( bucket );
  //#if F5EDEBUG
    Print("END OF TOP REDUCTION:  ");
  //#endif
    // for testing of correctness of ordering!
    //int testLength = 0;
    //kBucketClear( bucket, &sp, &testLength ); 
    // ---------------------------------------
    pTest( spTemp->p );
    pWrite( pHead(spTemp->p) ); 
    // for testing of correctness of ordering!
    //kBucketInit( bucket, sp, 0 ); 
    //sp = kBucketExtractLm( bucket );
    // ---------------------------------------
    //-------------------------------------------
    // now the reduction of the tail of sp starts
    //-------------------------------------------
    // reduce sp completely (Faugere does only top reductions!)
    while ( kBucketGetLm( bucket ) )
    {
      // search for reducers in the list gCurr
      temp = *gCurrFirst;
      while ( temp )
      {
        startagainTail:
        bucketExp = ~( pGetShortExpVector(kBucketGetLm(bucket)) );
              Print("HERE TAILREDUCTION AGAIN %p -- %p\n",temp, temp->next);
              Print("SPOLY RIGHT NOW: ");
              pWrite( spTemp->p );
              Print("POSSIBLE REDUCER: ");
              pWrite( temp->p );
              Print("BUCKET LM: ");
              pWrite( kBucketGetLm(bucket) );
              pTest( spTemp->p );
        if( isDivisibleGetMult( temp->p, temp->sExp, kBucketGetLm( bucket ), 
                                bucketExp, &multTemp, &isMult
                              ) 
          )
        {
          // if isMult => lm(sp) > lm(temp->p) => we need to multiply temp->p by 
          // multTemp and check this multiple by both criteria
          if( isMult )
          {
            // compute the multiple of the rule of temp & multTemp
            for( i=1; i<(currRing->N)+1; i++ )
            {
              multLabelTemp[i]  = multTemp[i] + temp->rewRule->label[i];
            }
            
            multLabelShortExp  = getShortExpVecFromArray( multLabelTemp );
            
            // test the multiplied label by both criteria 
            if( !criterion1( multLabelTemp, multLabelShortExp, f5Rules ) && 
                !criterion2( multLabelTemp, multLabelShortExp, temp->rewRule )
              )
            {  
              static unsigned long* multTempExp = (unsigned long*) 
                              omAlloc0( NUMVARS*sizeof(unsigned long) );
              static unsigned long* multLabelTempExp = (unsigned long*) 
                              omAlloc0( NUMVARS*sizeof(unsigned long) );
            
              getExpFromIntArray( multLabelTemp, multLabelTempExp, numVariables,
                                  shift, negBitmaskShifted, offsets
                                );   

              // if a higher label reduction should be done we do NOT reduce at all
              // we want to be fast in the tail reduction part
              if( expCmp( multLabelTempExp, spTemp->labelExp ) == 1 )
              {            
                isMult    = FALSE;
                redundant = TRUE;
                temp      = temp->next;
                Print("HERE TAILREDUCTION\n");
                if( temp )
                {
                  goto startagainTail;
                }
                else
                {
                  break;
                }
              }
              Print("HERE TAILREDUCTION2\n");
              poly multiplier = pOne();
              getExpFromIntArray( multTemp, multiplier->exp, numVariables, shift, 
                                  negBitmaskShifted, offsets
                                );
              // throw away the leading monomials of reducer and bucket
              p_SetCoeff( multiplier, pGetCoeff(kBucketGetLm(bucket)), currRing );
              pSetm( multiplier );
              pTest( multiplier );
              tempLength = pLength( temp->p->next );

              kBucketExtractLm(bucket);
              kBucket_Minus_m_Mult_p( bucket, multiplier, temp->p->next, 
                                      &tempLength 
                                    ); 
              if( canonicalize++ % 40 )
              {
                kBucketCanonicalize( bucket );
                canonicalize = 0;
              }
              isMult  = FALSE;
    Print("alalalaHERE1\n");
              if( kBucketGetLm( bucket ) )
              {
                temp  = *gCurrFirst;
              }
              else
              {
    Print("HERE2\n");
                goto kBucketLmZero;
              }
              goto startagainTail;
            }
          }
          // isMult = 0 => multTemp = 1 => we do not need to test temp->p by any
          // criterion again => go on with reduction steps
          else
          {
            number coeff  = pGetCoeff(kBucketGetLm(bucket));
            poly tempNeg  = pInit();
            // throw away the leading monomials of reducer and bucket
            tempNeg       = pCopy( temp->p );
            p_Mult_nn( tempNeg, coeff, currRing );
            pSetm( tempNeg );
            tempLength    = pLength( tempNeg->next );
            kBucketExtractLm(bucket);
            kBucket_Add_q( bucket, pNeg(tempNeg->next), &tempLength ); 
            if( canonicalize++ % 40 )
            {
              kBucketCanonicalize( bucket );
              canonicalize = 0;
            }
            if( kBucketGetLm( bucket ) )
            {
              temp  = *gCurrFirst;
            }
            else
            {
    Print("HERE2\n");
              goto kBucketLmZero;
            }
            goto startagainTail;
          } 
        }
        Print("WE SHOULD BE HERE!?! %p -- %p\n", temp, temp->next);
        temp  = temp->next;
        Print("WE SHOULD BE HERE!?! %p\n", temp);
      }
      // here we know that 
    Print("HERE1\n");
      pWrite( spTemp->p );
      pWrite( kBucketGetLm( bucket ) );
      spTemp->p =  p_Merge_q( spTemp->p, kBucketExtractLm(bucket), currRing );
    Print("HERE1\n");
      pWrite( spTemp->p );
      pTest(spTemp->p);
    }
    
    kBucketLmZero: 
    // otherwise sp is reduced to zero and we do not need to add it to gCurr
    // Note that even in this case the corresponding rule is already added to
    // rewRules list!
    if( spTemp->p )
    {
      Print("ORDER %ld -- %ld\n",p_GetOrder(spTemp->p,currRing), spTemp->p->exp[currRing->pOrdIndex]);
      pNorm( spTemp->p ); 
      // add sp together with rewRulesLast to gCurr!!!
      Lpoly* newElement     = (Lpoly*) omAlloc( sizeof(Lpoly) );
      newElement->next      = *gCurrFirst;
      newElement->p         = spTemp->p; 
      newElement->sExp      = pGetShortExpVector(spTemp->p); 
      newElement->rewRule   = rewRulesCurr; 
      newElement->redundant = redundant;
      // update pointer to last element in gCurr list
      *gCurrFirst           = newElement;
      Print( "ELEMENT ADDED TO GCURR: %p -- %p -- %p\n", *gCurrFirst, (*gCurrFirst)->p, rewRulesCurr );
      pWrite( (*gCurrFirst)->p );
      for( int lale = 1; lale < (currRing->N+1); lale++ )
      {
        Print( "%d  ",rewRulesCurr->label[lale] );
      }
      Print("\n"); 
      Print("SLABEL: %ld\n", rewRulesCurr->slabel);
      pTest( (*gCurrFirst)->p );
      //pWrite( gCurrLast->p );
      criticalPairPrev( *gCurrFirst, redGB, *f5Rules, cp, numVariables, 
                        shift, negBitmaskShifted, offsets 
                      );
      criticalPairCurr( *gCurrFirst, *f5Rules, cp, numVariables, 
                        shift, negBitmaskShifted, offsets 
                      );
    }
    else // spTemp->p == 0
    {
      pDelete( &spTemp->p );
    }
    //////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////
    // We need to add spTemp->p to gCurr
    // We need to get the corresponding rule for this!!!
    //////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////
    // go on to the next s-polynomial & rew rule in the list
    rewRulesCurr  = rewRulesCurr->next;
    Print("SPTEMP %p\n", spTemp);
    spTemp        = spTemp->next;
    Print("SPTEMP %p\n", spTemp);
  }
#if F5EDEBUG
    Print("CURRREDUCTION-END \n");
#endif
}



Cpair* sort(Cpair* cp, unsigned int length)
{
  // using merge sort 
  // Link: http://en.wikipedia.org/wiki/Merge_sort
  if(length == 1) 
  {
    return cp; 
  }
  else
  {
    int length1 = length / 2;
    int length2 = length - length1;
    // pointer to the start of the 2nd linked list for the next
    // iteration step of the merge sort
    Cpair* temp = cp;
    for(length=1; length < length1; length++)
    {
      temp = temp->next;
    }
    Cpair* cp2  = temp->next;
    temp->next  = NULL; 
    cp  = sort(cp, length1);
    cp2 = sort(cp2, length2);
    return merge(cp, cp2);
  }
}



Cpair* merge(Cpair* cp, Cpair* cp2)
{
  // initialize new, sorted list of critical pairs
  Cpair* cpNew = NULL;
  if( expCmp(cp->mLabelExp, cp2->mLabelExp) == -1 )
  {
    cpNew = cp;
    cp    = cp->next;
  }
  else
  { 
    cpNew = cp2;
    cp2   = cp2->next;
  }
  Cpair* temp = cpNew;
  while(cp!=NULL && cp2!=NULL)
  {
    if( expCmp(cp->mLabelExp, cp2->mLabelExp) == -1 )
    {
      temp->next  = cp;
      temp        = cp;
      cp          = cp->next;
    }
    else
    {
      temp->next  = cp2;
      temp        = cp2;
      cp2         = cp2->next;
    }
  }
  if(cp!=NULL)
  {
    temp->next  = cp;
  }
  else 
  {
    temp->next  = cp2;
  }

  return cpNew;
}



inline poly multInit( const int* exp, int numVariables, int* shift, 
                      unsigned long* negBitmaskShifted, int* offsets 
                    )
{
  poly np;
  omTypeAllocBin( poly, np, currRing->PolyBin );
  Print("POLY ADDRESS %p\n",np);
  p_SetRingOfLm( np, currRing );
  unsigned long* expTemp  =   (unsigned long*) omAlloc0(NUMVARS*
                              sizeof(unsigned long));
  getExpFromIntArray( exp, expTemp, numVariables, shift, negBitmaskShifted, 
                      offsets 
                    );
  static number n  = nInit(1);
  Print("EXPONENT VECTOR INTERNAL: %ld %ld %ld %ld\n", expTemp[0], expTemp[1], expTemp[2], expTemp[3]);
  p_MemCopy_LengthGeneral( np->exp, expTemp, NUMVARS );
  pNext(np) = NULL;
  pSetCoeff0(np, n);
  p_Setm( np, currRing );
  return np;
}



poly createSpoly( Cpair* cp, int numVariables, int* shift, unsigned long* negBitmaskShifted,
                  int* offsets, poly spNoether, int use_buckets, ring tailRing, 
                  TObject** R
                )
{
#if F5EDEBUG 
  Print("CREATESPOLY - BEGINNING %p\n", cp );
#endif
  LObject Pair( currRing );
  Pair.p1  = cp->p1;
  Print( "P1: %p\n", cp->p1 );
  //pWrite(cp->p1);
  Pair.p2  = cp->p2;
  Print( "P2: &p\n", cp->p2 );
  //pWrite(cp->p2);
  poly m1 = multInit( cp->mult1, numVariables, shift, 
                      negBitmaskShifted, offsets 
                    );
  Print("M1: %p ",m1);
  pWrite(m1);
  poly m2 = multInit( cp->mult2, numVariables, shift, 
                      negBitmaskShifted, offsets 
                    );
  Print("M2: %p ",m2);
  pWrite(m2);
#ifdef KDEBUG
  create_count_f5++;
#endif
  kTest_L( &Pair );
  poly p1 = Pair.p1;
  poly p2 = Pair.p2;


  poly last;
  Pair.tailRing = tailRing;

  assume(p1 != NULL);
  assume(p2 != NULL);
  assume(tailRing != NULL);

  poly a1 = pNext(p1), a2 = pNext(p2);
  number lc1 = pGetCoeff(p1), lc2 = pGetCoeff(p2);
  int co=0, ct = 3; // as lc1 = lc2 = 1 => 3=ksCheckCoeff(&lc1, &lc2); !!!

  int l1=0, l2=0;

  if (p_GetComp(p1, currRing)!=p_GetComp(p2, currRing))
  {
    if (p_GetComp(p1, currRing)==0)
    {
      co=1;
      p_SetCompP(p1,p_GetComp(p2, currRing), currRing, tailRing);
    }
    else
    {
      co=2;
      p_SetCompP(p2, p_GetComp(p1, currRing), currRing, tailRing);
    }
  }

  if (m1 == NULL)
    k_GetLeadTerms(p1, p2, currRing, m1, m2, tailRing);

  pSetCoeff0(m1, lc2);
  pSetCoeff0(m2, lc1);  // and now, m1 * LT(p1) == m2 * LT(p2)
  if (R != NULL)
  {
    if (Pair.i_r1 == -1)
    {
      l1 = pLength(p1) - 1;
    }
    else
    {
      l1 = (R[Pair.i_r1])->GetpLength() - 1;
    }
    if (Pair.i_r2 == -1)
    {
      l2 = pLength(p2) - 1;
    }
    else
    {
      l2 = (R[Pair.i_r2])->GetpLength() - 1;
    }
  }

  // get m2 * a2
  if (spNoether != NULL)
  {
    l2 = -1;
    a2 = tailRing->p_Procs->pp_Mult_mm_Noether( a2, m2, spNoether, l2, 
                                                tailRing,last
                                              );
    assume(l2 == pLength(a2));
  }
  else
    a2 = tailRing->p_Procs->pp_Mult_mm(a2, m2, tailRing,last);
#ifdef HAVE_RINGS
  if (!(rField_is_Domain(currRing))) l2 = pLength(a2);
#endif

  Pair.SetLmTail(m2, a2, l2, use_buckets, tailRing, last);

  // get m2*a2 - m1*a1
  Pair.Tail_Minus_mm_Mult_qq(m1, a1, l1, spNoether);

  // Clean-up time
  Pair.LmDeleteAndIter();
  p_LmDelete(m1, tailRing);

  if (co != 0)
  {
    if (co==1)
    {
      p_SetCompP(p1,0, currRing, tailRing);
    }
    else
    {
      p_SetCompP(p2,0, currRing, tailRing);
    }
  }

  pTest(Pair.GetLmCurrRing());
#if F5EDEBUG 
  Print("CREATESPOLY - END\n");
#endif
  return Pair.GetLmCurrRing();
}




///////////////////////////////////////////////////////////////////////////
// INTERREDUCTION STUFF: HANDLED A BIT DIFFERENT FROM SINGULAR KERNEL    //
///////////////////////////////////////////////////////////////////////////

// from kstd1.cc, strat->enterS points to one of these functions
void enterSMoraNF (LObject p, int atS,kStrategy strat, int atR = -1);


//-----------------------------------------------------------------------------
// BEGIN static stuff from kstd1.cc 
// This is needed for the computation of strat, but as it is static in kstd1.cc
// we need to copy it!
//-----------------------------------------------------------------------------

static int doRed (LObject* h, TObject* with,BOOLEAN intoT,kStrategy strat)
{
  poly hp;
  int ret;
#if KDEBUG > 0
  kTest_L(h);
  kTest_T(with);
#endif
  // Hmmm ... why do we do this -- polys from T should already be normalized
  if (!TEST_OPT_INTSTRATEGY)  
    with->pNorm();
#ifdef KDEBUG
  if (TEST_OPT_DEBUG)
  {
    PrintS("reduce ");h->wrp();PrintS(" with ");with->wrp();PrintLn();
  }
#endif
  if (intoT)
  {
    // need to do it exacly like this: otherwise
    // we might get errors
    LObject L= *h;
    L.Copy();
    h->GetP();
    h->SetLength(strat->length_pLength);
    ret = ksReducePoly(&L, with, strat->kNoetherTail(), NULL, strat);
    if (ret)
    {
      if (ret < 0) return ret;
      if (h->tailRing != strat->tailRing)
        h->ShallowCopyDelete(strat->tailRing,
                             pGetShallowCopyDeleteProc(h->tailRing,
                                                       strat->tailRing));
    }
    enterT(*h,strat);
    *h = L;
  }
  else
    ret = ksReducePoly(h, with, strat->kNoetherTail(), NULL, strat);
#ifdef KDEBUG
  if (TEST_OPT_DEBUG)
  {
    PrintS("to ");h->wrp();PrintLn();
  }
#endif
  return ret;
}



/*2
* reduces h with elements from T choosing first possible
* element in T with respect to the given ecart
* used for computing normal forms outside kStd
*/
static poly redMoraNF (poly h,kStrategy strat, int flag)
{
  LObject H;
  H.p = h;
  int j = 0;
  int z = 10;
  int o = H.SetpFDeg();
  H.ecart = pLDeg(H.p,&H.length,currRing)-o;
  if ((flag & 2) == 0) cancelunit(&H,TRUE);
  H.sev = pGetShortExpVector(H.p);
  unsigned long not_sev = ~ H.sev;
  loop
  {
    if (j > strat->tl)
    {
      return H.p;
    }
    if (TEST_V_DEG_STOP)
    {
      if (kModDeg(H.p)>Kstd1_deg) pLmDelete(&H.p);
      if (H.p==NULL) return NULL;
    }
    if  (p_LmShortDivisibleBy ( strat->T[j].GetLmTailRing(), strat->sevT[j], 
                                H.GetLmTailRing(), not_sev, strat->tailRing
                              )
        )
    {
      //if (strat->interpt) test_int_std(strat->kIdeal);
      /*- remember the found T-poly -*/
      poly pi = strat->T[j].p;
      int ei = strat->T[j].ecart;
      int li = strat->T[j].length;
      int ii = j;
      /*
      * the polynomial to reduce with (up to the moment) is;
      * pi with ecart ei and length li
      */
      loop
      {
        /*- look for a better one with respect to ecart -*/
        /*- stop, if the ecart is small enough (<=ecart(H)) -*/
        j++;
        if (j > strat->tl) break;
        if (ei <= H.ecart) break;
        if (((strat->T[j].ecart < ei)
          || ((strat->T[j].ecart == ei)
        && (strat->T[j].length < li)))
        && pLmShortDivisibleBy(strat->T[j].p,strat->sevT[j], H.p, not_sev))
        {
          /*
          * the polynomial to reduce with is now;
          */
          pi = strat->T[j].p;
          ei = strat->T[j].ecart;
          li = strat->T[j].length;
          ii = j;
        }
      }
      /*
      * end of search: have to reduce with pi
      */
      z++;
      if (z>10)
      {
        pNormalize(H.p);
        z=0;
      }
      if ((ei > H.ecart) && (!strat->kHEdgeFound))
      {
        /*
        * It is not possible to reduce h with smaller ecart;
        * we have to reduce with bad ecart: H has to enter in T
        */
        doRed(&H,&(strat->T[ii]),TRUE,strat);
        if (H.p == NULL)
          return NULL;
      }
      else
      {
        /*
        * we reduce with good ecart, h need not to be put to T
        */
        doRed(&H,&(strat->T[ii]),FALSE,strat);
        if (H.p == NULL)
          return NULL;
      }
      /*- try to reduce the s-polynomial -*/
      o = H.SetpFDeg();
      if ((flag &2 ) == 0) cancelunit(&H,TRUE);
      H.ecart = pLDeg(H.p,&(H.length),currRing)-o;
      j = 0;
      H.sev = pGetShortExpVector(H.p);
      not_sev = ~ H.sev;
    }
    else
    {
      j++;
    }
  }
}

//------------------------------------------------------------------------
// END static stuff from kstd1.cc 
//------------------------------------------------------------------------

void prepRedGBReduction(kStrategy strat, ideal F, ideal Q, int lazyReduce)
{
  int i;
  LObject   h;

  strat->kHEdgeFound = ppNoether != NULL;
  strat->kNoether=pCopy(ppNoether);
  test|=Sy_bit(OPT_REDTAIL);
  if (TEST_OPT_STAIRCASEBOUND
  && (0<Kstd1_deg)
  && ((!strat->kHEdgeFound)
    ||(TEST_OPT_DEGBOUND && (pWTotaldegree(strat->kNoether)<Kstd1_deg))))
  {
    pDelete(&strat->kNoether);
    strat->kNoether=pOne();
    pSetExp(strat->kNoether,1, Kstd1_deg+1);
    pSetm(strat->kNoether);
    strat->kHEdgeFound=TRUE;
  }
  initBuchMoraCrit(strat);
  initBuchMoraPos(strat);
  initMora(F,strat);
  strat->enterS = enterSMoraNF;
  /*- set T -*/
  strat->tl = -1;
  strat->tmax = setmaxT;
  strat->T = initT();
  strat->R = initR();
  strat->sevT = initsevT();
  /*- set S -*/
  strat->sl = -1;
  /*- init local data struct.-------------------------- -*/
  /*Shdl=*/initS(F,Q,strat);
  if ((strat->ak!=0)
  && (strat->kHEdgeFound))
  {
    if (strat->ak!=1)
    {
      pSetComp(strat->kNoether,1);
      pSetmComp(strat->kNoether);
      poly p=pHead(strat->kNoether);
      pSetComp(p,strat->ak);
      pSetmComp(p);
      p=pAdd(strat->kNoether,p);
      strat->kNoether=pNext(p);
      p_LmFree(p,currRing);
    }
  }
  if (TEST_OPT_INTSTRATEGY && ((lazyReduce & KSTD_NF_LAZY)==0))
  {
    for (i=strat->sl; i>=0; i--)
      pNorm(strat->S[i]);
  }
  
  assume(!(idIs0(F)&&(Q==NULL)));

// lazy_reduce flags: can be combined by |
//#define KSTD_NF_LAZY   1
  // do only a reduction of the leading term
//#define KSTD_NF_ECART  2
  // only local: recude even with bad ecart
  //poly   p;

  //if ((idIs0(F))&&(Q==NULL))
  //  return pCopy(q); /*F=0*/
  //strat->ak = si_max(idRankFreeModule(F),pMaxComp(q));
  /*- creating temp data structures------------------- -*/
  strat->kHEdgeFound = ppNoether != NULL;
  strat->kNoether    = pCopy(ppNoether);
  test|=Sy_bit(OPT_REDTAIL);
  test&=~Sy_bit(OPT_INTSTRATEGY);
  if (TEST_OPT_STAIRCASEBOUND
  && (! TEST_V_DEG_STOP)
  && (0<Kstd1_deg)
  && ((!strat->kHEdgeFound)
    ||(TEST_OPT_DEGBOUND && (pWTotaldegree(strat->kNoether)<Kstd1_deg))))
  {
    pDelete(&strat->kNoether);
    strat->kNoether=pOne();
    pSetExp(strat->kNoether,1, Kstd1_deg+1);
    pSetm(strat->kNoether);
    strat->kHEdgeFound=TRUE;
  }
  initBuchMoraCrit(strat);
  initBuchMoraPos(strat);
  initMora(F,strat);
  strat->enterS = enterSMoraNF;
  /*- set T -*/
  strat->tl = -1;
  strat->tmax = setmaxT;
  strat->T = initT();
  strat->R = initR();
  strat->sevT = initsevT();
  /*- set S -*/
  strat->sl = -1;
  /*- init local data struct.-------------------------- -*/
  /*Shdl=*/initS(F,Q,strat);
  if ((strat->ak!=0)
  && (strat->kHEdgeFound))
  {
    if (strat->ak!=1)
    {
      pSetComp(strat->kNoether,1);
      pSetmComp(strat->kNoether);
      poly p=pHead(strat->kNoether);
      pSetComp(p,strat->ak);
      pSetmComp(p);
      p=pAdd(strat->kNoether,p);
      strat->kNoether=pNext(p);
      p_LmFree(p,currRing);
    }
  }
  if ((lazyReduce & KSTD_NF_LAZY)==0)
  {
    for (i=strat->sl; i>=0; i--)
      pNorm(strat->S[i]);
  }
  /*- puts the elements of S also to T -*/
  for (i=0; i<=strat->sl; i++)
  {
    h.p = strat->S[i];
    h.ecart = strat->ecartS[i];
    if (strat->sevS[i] == 0) strat->sevS[i] = pGetShortExpVector(h.p);
    else assume(strat->sevS[i] == pGetShortExpVector(h.p));
    h.length = pLength(h.p);
    h.sev = strat->sevS[i];
    h.SetpFDeg();
    enterT(h,strat);
  }
}



poly reduceByRedGBCritPair  ( Cpair* cp, kStrategy strat, int numVariables, 
                              int* shift, unsigned long* negBitmaskShifted, 
                              int* offsets, int lazyReduce
                            )
{
  poly  p;
  int   i;
  int   j;
  int   o;
  BITSET save_test=test;
  // create the s-polynomial corresponding to the critical pair cp
  poly q = createSpoly( cp, numVariables, shift, negBitmaskShifted, offsets );
  /*- compute------------------------------------------- -*/
  p = pCopy(q);
  deleteHC(&p,&o,&j,strat);
  if (TEST_OPT_PROT) 
  { 
    PrintS("r"); 
    mflush(); 
  }
  if (p!=NULL) p = redMoraNF(p,strat, lazyReduce & KSTD_NF_ECART);
  if ((p!=NULL)&&((lazyReduce & KSTD_NF_LAZY)==0))
  {
    if (TEST_OPT_PROT) 
    { 
      PrintS("t"); mflush(); 
    }
    Print("CURRING TYP: %p\n",currRing->typ);
    Print("RED TAIL REDUCTION DONE\n");
    p = redtail(p,strat->sl,strat);
  }
  test=save_test;
  if (TEST_OPT_PROT) PrintLn();
  return p;
}



// NEEDED AT ALL?
poly reduceByRedGBPoly( poly q, kStrategy strat, int lazyReduce )
{
  poly  p;
  int   i;
  int   j;
  int   o;
  BITSET save_test=test;
  ////////////////////////////////////////////////////////
  // TODO: critical pair -> s-polynomial
  //       q should be the s-polynomial!!!!
  ////////////////////////////////////////////////////////
  
  /*- compute------------------------------------------- -*/
  Print("dddd\n");
  pTest( q );
  Print("dddd\n");
  p = pCopy(q);
  deleteHC(&p,&o,&j,strat);
  if (TEST_OPT_PROT) 
  { 
    PrintS("r"); 
    mflush(); 
  }
  if (p!=NULL) p = redMoraNF(p,strat, lazyReduce & KSTD_NF_ECART);
  if ((p!=NULL)&&((lazyReduce & KSTD_NF_LAZY)==0))
  {
    if (TEST_OPT_PROT) 
    { 
      PrintS("t"); 
      mflush(); 
    }
    p = redtail(p,strat->sl,strat);
  }
  test=save_test;
  if (TEST_OPT_PROT) PrintLn();
  return p;
}



void clearStrat(kStrategy strat, ideal F, ideal Q)
{
  int i;
  cleanT(strat);
  omFreeSize((ADDRESS)strat->T,strat->tmax*sizeof(TObject));
  omFreeSize((ADDRESS)strat->ecartS,IDELEMS(strat->Shdl)*sizeof(int));
  omFreeSize((ADDRESS)strat->sevS,IDELEMS(strat->Shdl)*sizeof(unsigned long));
  omFreeSize((ADDRESS)strat->NotUsedAxis,(pVariables+1)*sizeof(BOOLEAN));
  omFree(strat->sevT);
  omFree(strat->S_2_R);
  omFree(strat->R);

  if ((Q!=NULL)&&(strat->fromQ!=NULL))
  {
    i=((IDELEMS(Q)+IDELEMS(F)+15)/16)*16;
    omFreeSize((ADDRESS)strat->fromQ,i*sizeof(int));
    strat->fromQ=NULL;
  }
  pDelete(&strat->kHEdge);
  pDelete(&strat->kNoether);
  if ((TEST_OPT_WEIGHTM)&&(F!=NULL))
  {
    pRestoreDegProcs(pFDegOld, pLDegOld);
    if (ecartWeights)
    {
      omFreeSize((ADDRESS *)&ecartWeights,(pVariables+1)*sizeof(short));
      ecartWeights=NULL;
    }
  }
  idDelete(&strat->Shdl);

}




///////////////////////////////////////////////////////////////////////////
// MEMORY & INTERNAL EXPONENT VECTOR STUFF: HANDLED A BIT DIFFERENT FROM //
// SINGULAR KERNEL                                                       //
///////////////////////////////////////////////////////////////////////////

unsigned long getShortExpVecFromArray(int* a, ring r)
{
  unsigned long ev  = 0; // short exponent vector
  unsigned int  n   = BIT_SIZEOF_LONG / r->N; // number of bits per exp
  unsigned int  i   = 0,  j = 1;
  unsigned int  m1; // highest bit which is filled with (n+1)   

  if (n == 0)
  {
    if (r->N<2*BIT_SIZEOF_LONG)
    {
      n=1;
      m1=0;
    }
    else
    {
      for (; j<=(unsigned long) r->N; j++)
      {
        if (a[j] > 0) i++;
        if (i == BIT_SIZEOF_LONG) break;
      }
      if (i>0)
      ev = ~((unsigned long)0) >> ((unsigned long) (BIT_SIZEOF_LONG - i));
      return ev;
    }
  }
  else
  {
    m1 = (n+1)*(BIT_SIZEOF_LONG - n*r->N);
  }

  n++;
  while (i<m1)
  {
    ev |= GetBitFieldsF5e(a[j] , i, n);
    i += n;
    j++;
  }

  n--;
  while (i<BIT_SIZEOF_LONG)
  {
    ev |= GetBitFieldsF5e(a[j] , i, n);
    i += n;
    j++;
  }
  return ev;
}


inline void getExpFromIntArray( const int* exp, unsigned long* r, 
                                int numVariables, int* shift, unsigned long* 
                                negBitmaskShifted, int* offsets
                              )
{
  register int i = numVariables;
  for( ; i; i--)
  {
    register int shiftL   =   shift[i];
    Print("%d. EXPONENT CREATION %d\n",i,exp[i]);
    unsigned long ee      =   exp[i];
    ee                    =   ee << shiftL;
    register int offsetL  =   offsets[i];
    r[offsetL]            &=  negBitmaskShifted[i];
    r[offsetL]            |=  ee;
  }
  r[currRing->pCompIndex] = exp[0];
  Print("COMPONENT CREATION: %d\n", exp[0]);
  Print("EXPONENT VECTOR INTERNAL AT CREATION: %ld %ld %ld %ld\n", r[0], r[1], r[2], r[3]);
}



/// my own GetBitFields
/// @sa GetBitFields
inline unsigned long GetBitFieldsF5e( int e, unsigned int s, 
                                      unsigned int n
                                    )
{
#define Sy_bit_L(x)     (((unsigned long)1L)<<(x))
  unsigned int i = 0;
  unsigned long  ev = 0L;
  assume(n > 0 && s < BIT_SIZEOF_LONG);
  do
  {
    assume(s+i < BIT_SIZEOF_LONG);
    if ((long) e > (long) i) ev |= Sy_bit_L(s+i);
    else break;
    i++;
  }
  while (i < n);
  return ev;
}



inline int expCmp(const unsigned long* a, const unsigned long* b)
{
  p_MemCmp_LengthGeneral_OrdGeneral(a, b, currRing->CmpL_Size, currRing->ordsgn,
                                      return 0, return 1, return -1);
}


static inline BOOLEAN isDivisibleGetMult  ( poly a, unsigned long sev_a, poly b, 
                                            unsigned long not_sev_b, int** mult,
                                            BOOLEAN* isMult
                                          )
{
#if F5EDEBUG
    Print("ISDIVISIBLE-BEGINNING \n");
#endif
  pWrite(pHead(a));
  Print("%ld\n",pGetShortExpVector(a));
  Print("%ld\n",~pGetShortExpVector(a));
  pWrite(pHead(b));
  Print("%ld\n",~pGetShortExpVector(b));
  Print("BOOLEAN? %ld\n",(pGetShortExpVector(a) & (~pGetShortExpVector(b))));
  Print("BOOLEAN? %ld\n",(sev_a & not_sev_b));
  p_LmCheckPolyRing1(a, currRing);
  p_LmCheckPolyRing1(b, currRing);
  if (sev_a & not_sev_b)
  {
    Print("SEVA %ld\n",sev_a);
    Print("SEVB %ld\n", not_sev_b);
    pAssume1(_p_LmDivisibleByNoComp(a, currRing, b, currRing) == FALSE);
    *isMult = FALSE;
#if F5EDEBUG
    Print("ISDIVISIBLE-END1 \n");
#endif
    return FALSE;
  }
  if (p_GetComp(a, currRing) == 0 || p_GetComp(a,currRing) == p_GetComp(b,currRing))
  {
    int i=currRing->N;
    pAssume1(currRing->N == currRing->N);
    *mult[0]  = p_GetComp(a,currRing);
    do
    {
      Print("%d ---- %d\n",p_GetExp(a,i,currRing), p_GetExp(b,i,currRing) );
      if (p_GetExp(a,i,currRing) > p_GetExp(b,i,currRing))
      {
        *isMult = FALSE;
#if F5EDEBUG
    Print("ISDIVISIBLE-END2 \n");
#endif
        return FALSE;
      }
      (*mult)[i] = (p_GetExp(b,i,currRing) - p_GetExp(a,i,currRing)); 
      if( ((*mult)[i])>0 )
      {
        *isMult = TRUE;
      }
      i--;
    }
    while (i);
    (*mult)[0]  = 0;
#ifdef HAVE_RINGS
#if F5EDEBUG
    Print("ISDIVISIBLE-END3 \n");
#endif
    return nDivBy(p_GetCoeff(b, r), p_GetCoeff(a, r));
#else
#if F5EDEBUG
    Print("ISDIVISIBLE-END4 \n");
#endif
    return TRUE;
#endif
  }
#if F5EDEBUG
    Print("ISDIVISIBLE-END5 \n");
#endif
  return FALSE;
}
#endif
// HAVE_F5C
