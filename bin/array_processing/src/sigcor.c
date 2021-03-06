/* 
This program will correlate signals among array channels, after delays have
been applied for the known (or estimated) ray parameters.  Station traces are
aligned to the reference site.  There is no correction for instrument response,
so all traces should have same response.

In the case that the input slowness is zero, the program computes the observed
delays among the array channels.

Usage: sigcor tstart siglen laglen filter sr slow baz arrayname refsta dbname dfile

where

  tstart = start time of signal in epoch seconds at station "refsta"
  siglen = signal length to be used in seconds
  laglen = lag length to be used in seconds for cross-correlation.  This is an
           estimate and will be checked and changed, if necessary, in the code.
  filter = filter specification in BRTT syntax, like "BW 0.5 4 8.0 4" (Must be
           put in quotes.)
  sr = desired sampling rate (<= actual sampling rate).  If high sample rate
       channels and lower sample rate channels are used, this specifies the
       lower rate so that all channels will have same rate.
  slow   = signal slowness (s/km) 
  baz    = signal azimuth (degrees CW from north)
  arrayname = name of array
  refsta = name of station to which signals will be referenced.  This station
           should be near center of array.
  dbname = database name of input data 
  dfile = file name for where results will be stored

Note: ".arr" is appended in program to arrayname to make file name for file
      containing array parameters.

The output file contains 4 sets of data, each having one row per station.
1 - distances among stations (km)
2 - back azimuths from station to station (degrees)
3 - ccc's (cross-correlation coefficients -- lies in the range [-1,1])
4 - relative computed delays among stations (seconds) AFTER aligning according
    to the specified slowness and azimuth.  These then are residual delays.

Each is a symmetric matrix.  To be precise, the azimuths should be computed
for forward and backward between stations; however, it is assumed that the 
stations occupy such a small area that the reverse azimuth between stations
is simply 180 degrees different from the forward azimuth.

*/

#include <stdio.h>
#include <math.h>
#include "db.h"
#include "coords.h"
#include "stock.h"
#include "tttaup.h"

#define MXSTA 100
#define MXPTS 100001
#define rad2dg 57.2958
#define dg2km 111.19
#define pi 3.14159
#define debug 1

double  get_shift(double,double,double,double,double);
int     align_signals(double,double,double);
int     get_stat(float*,int,double*,double*);
int     trgetwf(Dbptr, Arr **,float **,int *,double,double,double *,double *,
        int *,int *,void *);
int     trfilter_segs(int , int *, double *, float **, char *);
int     get_tau(float y1[],float y2[],double t1,double t2,int npts,
        float laglen,double sr,double *tau,float *ccc,float *cccp);

int     nsta,nexist,spts;
double  rx[MXSTA],ry[MXSTA],delay[MXSTA];
float   *s[MXSTA];

int main(int argc, char *argv[])
{
  int      iret,index,nbytes,i,j,idec,mpts[MXSTA];
  long int ns,nz;
  double   slow,baz,rdec,siglen,lat[MXSTA],lon[MXSTA],reflon,reflat,
           distance,azimuth,del,delo,tstart,tend,ts[MXSTA],te[MXSTA],
           shift[MXSTA],mean,stdv,zero,maxneg,maxpos,kx,ky,tau,sr,sro,calib,fnyq;
  float    ccc,cccp,laglen;
  float    s1[MXPTS],s2[MXPTS];
  char     asta[7],refsta[7],searchExpr[200],corner[7];
  char     sta[MXSTA][7],chan[MXSTA][9];
  char     *arrayfile,*arrayname,*dbname,*filter,*decfilter,*dfile,*sfile;
  Dbptr    db,dba,dbse,dbi,dbs,dbw,dbz;
  FILE     *fp,*fp2;
 
  arrayname = malloc(9);
  arrayfile = malloc(13);
  dbname    = malloc(80);
  filter    = malloc(20);
  decfilter = malloc(20);
  dfile     = malloc(80);

  sfile     = malloc(80);
  strcpy(sfile,"signals.dat");

  if (argc < 12) 
  {
    printf("Usage: %s tstart siglen laglen filter sr slow baz arrayname refsta dbname dfile\n",argv[0]);
    return 1;
  }

  sscanf(argv[1],"%lf",&tstart);
  sscanf(argv[2],"%lf",&siglen);
  sscanf(argv[3],"%f",&laglen);
  filter = argv[4]; 
  sscanf(argv[5],"%lf",&sr); 
  sscanf(argv[6],"%lf",&slow); 
  sscanf(argv[7],"%lf",&baz); 
  arrayname = argv[8]; 
  sscanf(argv[9],"%s",refsta);
  dbname = argv[10];
  dfile = argv[11];

/*Open the array file and get array info.*/
  memcpy(arrayfile,"\0",1);
  strcpy(arrayfile,arrayname);
  strcat(arrayfile,".arr");
  if ( (fp = fopen(arrayfile,"r")) == NULL)
  {
    complain(0,"Could not open array file.\n");
    return 1;
  }
  nsta = 0;
  while(fscanf(fp, "%s %s %lf", sta[nsta],chan[nsta],&delay[nsta]) != EOF)
  {
    nsta = nsta + 1;
  }
  printf("# stations to be used in cross-correlations  = %d\n",nsta);
  fclose(fp);

/*Open the database and get required info on the array.*/

  if (dbopen(dbname,"r",&db) < 0)
  {
    complain(0,"Could not open database.\n");
    return 1;
  }

  dbi  = dblookup(db,0,"instrument",0,0);
  dbse = dblookup(db,0,"sensor",0,0);
  dbs  = dblookup(db,0,"site",0,0);
  dbw  = dblookup(db,0,"wfdisc",0,0);

  kx = slow*cos(pi/2 - baz/rad2dg);
  ky = slow*sin(pi/2 - baz/rad2dg);
  printf("slow,baz,kx,ky = %f %f %f %f\n",slow,baz,kx,ky);

/*Find reference station and put all station coordinates relative to it.*/
  dbquery(dbs,dbRECORD_COUNT,&ns);
  for(dbs.record=0;dbs.record<ns;dbs.record++)
  {
    dbgetv(dbs,NULL,"sta",asta,"lat",&reflat,"lon",&reflon,NULL);
    if (strcmp(refsta,asta) == 0) break;
  }
  printf("refsta,reflat,reflon = %s %f %f\n",refsta,reflat,reflon);
  for (j=0;j<nsta;j++)
  {
    sprintf( searchExpr, "sta =~ /%s/",sta[j]);
    dba = dbsubset(dbs,searchExpr,NULL);
    dba.record = 0;
    dbgetv(dba,NULL,"lat",&lat[j],"lon",&lon[j],NULL);
/*  Note: dist returns distance in radians and azimuth in radians.*/
    dist(reflat/rad2dg,reflon/rad2dg,lat[j]/rad2dg,lon[j]/rad2dg,&distance,&azimuth);
    distance = distance*rad2dg*dg2km;
    rx[j]=distance*cos(pi/2 - azimuth);
    ry[j]=distance*sin(pi/2 - azimuth);
    shift[j] = get_shift(kx,ky,rx[j],ry[j],delay[j]);
    printf("%d %s %7.2lf %7.2lf %5.2lf\n",j+1,sta[j],rx[j],ry[j],delay[j]);
  }

/*Find largest delays in order to increase window.  This assumes the reference
  station is near the center of the array.*/
  maxneg = 0.0;
  maxpos = 0.0;
  for(j=0;j<nsta;j++)
  {
    if(shift[j] > maxpos) maxpos = shift[j];
    if(shift[j] < maxneg) maxneg = shift[j];
  }
  tend =  tstart + siglen - maxneg;
  tstart = tstart - maxpos;
  spts = (tend - tstart)*sr;

/*Adjust laglen to make sure it gets signals overlapped.*/
  if (laglen <  maxpos) 
  {
    laglen =  maxpos;
    printf("Note: laglen increased to %f\n",laglen);
  }
  if (laglen < -maxneg) 
  {
    laglen = -maxneg; 
    printf("Note: laglen increased to %f\n",laglen);
  }

/*Get the waveforms for correlation window.*/
  printf("tstart,tend = %f %f\n",tstart,tend);

  nexist = 0;
  for (j=0;j<nsta;j++)
  {
/*  Not all stations in the array may have a recording.  Before starting, just 
    fill the waveforms with zeroes.  If any waveforms do not have full length 
    when returned from trgetwf, they will be filled in with zeroes also.*/
    s[j] = malloc(MXPTS*sizeof(float));
    for (i=0;i<MXPTS;i++) s[j][i] = 0.0;
    sprintf( searchExpr, "sta =~ /%s/ && chan =~ /%s/ && time <= %lf && endtime >= %lf",sta[j],chan[j],tstart,tend);
    dbz = dbsubset(dbw,searchExpr,NULL);
    dbquery(dbz,dbRECORD_COUNT,&nz);
    if (nz == 0) 
    { 
      mpts[j] = 0;
      complain(0,"No wfdisc records found for station %s.\n",sta[j]);
      continue;
    }
    dbz.record = 0;
    dbgetv(dbz,NULL,"calib",&calib,"samprate",&sro,NULL);
    if (calib == 0.0) calib = 1.0;
    nbytes =  MXPTS*sizeof(float);
/*  Note: trgetwf does NOT apply calibration factor, so we will.*/
    iret=trgetwf(dbz,NULL,&s[j],&nbytes,tstart,tend,&ts[j],&te[j],&mpts[j],0,0);
    if (iret != 0)
    {
      mpts[j] = 0;
      complain(0,"error in getting data for station %s\n",sta[j]);
      continue;
    }
    else
    {
      mpts[j] = spts;
    }
    for (i=0;i<mpts[j];i++) s[j][i] = s[j][i]*calib;
    nexist = nexist + 1;
    printf("%2d %4s %15.3f %15.3f %5d\n",j,sta[j],ts[j],te[j],mpts[j]);
  }
  printf("# stations having waveforms to correlate = %d\n",nexist);
  if (nexist == 0) 
  {
    complain(0,"No waveforms available for this time -- cannot continue.\n");
    return 1;
  }

/*Now do resampling, if needed.  Find the original sampling rate first.  The
  resampling is done in place.*/

  for(j=0;j<nsta;j++)
  {
    if (mpts[j] == 0) continue;
    delo = 1/sro;
    del = 1/sr;
    rdec = (sro/sr);
    if (rdec - floor(rdec) > 0.5) 
     idec = ceil(rdec);
    else
      idec = floor(rdec);
/*  printf("idec = %d\n",idec);*/
/*  Check old and new sample rates.*/
    if (fabs(rdec - idec) > 0.01) 
    {
      complain(0,"Old sample rate over new sample rate != integer -- cannot continue.\n");
      complain(0,"Old sample rate = %f; new sample rate = %f\n",sro,sr);
      return 1;
    }
    if (idec == 0) 
    {
      complain(0,"Old sample rate over new sample rate < 1 -- cannot resample.\n");
      complain(0,"Old sample rate = %f; new sample rate = %f\n",sro,sr);
      return 1;
    }        

/*  Check if new rate requires decimation; if so, do it.*/
    if (idec > 1) 
    {
/*    Filter first to prevent aliasing.*/
      fnyq = 1.0/(2.0*del);
      strcpy(decfilter,"\0");
      strcat(decfilter,"BW 0.0 0 ");
      sprintf(corner,"%6.2f",0.8*fnyq);
      strcat(decfilter,corner);
      strcat(decfilter," 4");
      if (debug) printf("j,idec,decfilter,mpts,del = %d %d %s %d %f\n",j,idec,decfilter,mpts[j],del);
      iret = trfilter_segs(1,&mpts[j],&del,&s[j],decfilter);
      if (iret != 0)
      {
        complain(0,"error in filtering waveform %d = %d\n",j,iret);
        return 1;
      }
      for(i=0;i<spts;i++) s[j][i] = s[j][i*idec];
    }
  }
  printf("retrieved waveforms\n");
  printf("# points in signal window = %d\n",spts);
  dbclose(db);

/*Remove mean of traces.*/
  for(j=0;j<nsta;j++)
  { 
    if (mpts[j] == 0) continue;
    iret = get_stat(s[j],spts,&mean,&stdv);
    printf("sta,mean,stdv = %4s %15.3f %15.3f\n",sta[j],mean,stdv);
    for(i=0;i<spts;i++) s[j][i] = s[j][i] - mean;
  }

/*Filter the data prior to using if requested.*/
  if (strlen(filter) > 0)
  {
    for(j=0;j<nsta;j++)
    {
      if (mpts[j] == 0) continue;
      iret = trfilter_segs(1,&spts,&del,&s[j],filter);
      if (iret != 0)
      {
        complain(0,"error in filtering waveform %d = %d\n",j,iret);
        return 1;
      }
    }
  }

/*Align signals.*/ 
  if (kx != 0.0 || ky != 0.0) align_signals(del,kx,ky);

/*Cut out a window equal to the original window length before cross-correlating
  them.  Redefine spts based on originally requested window.*/
  spts = siglen*sr;

  index = maxpos*sr;
  printf("maxpos,sr = %f %f\n",maxpos,sr);
  if (index != 0)
  {
    for(j=0;j<nsta;j++)
    {
      if (mpts[j] == 0) continue;
      for(i=0;i<spts;i++) s[j][i] = s[j][i+index];
    }
  }

/*Remove mean of traces again.*/
  for(j=0;j<nsta;j++)
  { 
    if (mpts[j] == 0) continue;
    iret = get_stat(s[j],spts,&mean,&stdv);
    printf("   sta,mean,stdv = %4s %15.3f %15.3f\n",sta[j],mean,stdv);
    for(i=0;i<spts;i++) s[j][i] = s[j][i] - mean;
  }

  if ( (fp = fopen(dfile,"w")) == NULL)
  {
    complain(0,"Could not open output file.\n");
    return 1;
  }
 
/*Compute interstation distances and azimuths and write to output file as
  matrices.*/

  for (j=0;j<nsta;j++)
  {
    fprintf(fp,"%-4s",sta[j]);
    for (i=0;i<nsta;i++)
    {
      distance = sqrt((rx[j] - rx[i])*(rx[j] - rx[i])+(ry[j] - ry[i])*(ry[j] - ry[i]));
      fprintf(fp," %5.2f",distance);
    }
    fprintf(fp,"\n");
  }

  for (j=0;j<nsta;j++)
  {
    fprintf(fp,"%-4s",sta[j]);
    for (i=0;i<nsta;i++)
    {
      azimuth = rad2dg*atan2(ry[j] - ry[i],rx[j] - rx[i]);
      fprintf(fp," %5.0f",azimuth);
    }
    fprintf(fp,"\n");
  }    

/*Write the signals to a file -- debugging only.
  printf("spts = %d\n",spts);
  printf("nsta = %d\n",nsta);
  fp2 = fopen(sfile,"wb");
  for (j=0;j<nsta;j++)
    fwrite(s[j],sizeof(float),spts,fp2);
  fclose(fp2);
*/

/*Cross-correlate all signal pairs.  For those stations with no data, the xcor 
  values will be set to -1. All requested stations will have a row in the 
  output matrix of xcor's.*/
 
/*Compute and write the ccc's.*/
  for(j=0;j<nsta;j++)
  {
    fprintf(fp,"%-4s",sta[j]);
    if (mpts[j] > 0)
      iret = get_stat(s[j],spts,&mean,&stdv);
    else 
      stdv = 0.0;
    if (stdv > 0.0)
    {
      memcpy(s1,s[j],4*spts);
      for(i=0;i<nsta;i++)
      {
        if (mpts[i] > 0)
          iret = get_stat(s[i],spts,&mean,&stdv);
        else
          stdv = 0.0;
        if (stdv > 0.0)
        {
          memcpy(s2,s[i],4*spts);
          zero = 0.0;
          iret = get_tau(s1,s2,ts[j],ts[i],spts,laglen,sr,&tau,&ccc,&cccp);
          fprintf(fp," %5.2f",ccc);
        }
        else
        {
          ccc = -1.0;
          fprintf(fp," %5.2f",ccc);
        }
      }
      fprintf(fp,"\n");
    }
    else
    {
      for(i=0;i<nsta;i++)
      {
        ccc = -1.0;
        fprintf(fp," %5.2f",ccc);
      }
      fprintf(fp,"\n");
    }
  }

/*Repeat the whole thing to write the tau's.*/
  for(j=0;j<nsta;j++)
  {
    fprintf(fp,"%-4s",sta[j]);
    if (mpts[j] > 0)
      iret = get_stat(s[j],spts,&mean,&stdv);
    else
      stdv =0.0;
    if (stdv > 0.0)
    {
      memcpy(s1,s[j],4*spts);
      for(i=0;i<nsta;i++)
      {
        if (mpts[i] > 0)
          iret = get_stat(s[i],spts,&mean,&stdv);
        else
          stdv = 0.0;
        if (stdv > 0.0)
        {
          memcpy(s2,s[i],4*spts);
          zero = 0.0;
          iret = get_tau(s1,s2,ts[j],ts[i],spts,laglen,sr,&tau,&ccc,&cccp);
          fprintf(fp," %5.2f",tau);
        }
        else
        {
          tau = -9.99;
          fprintf(fp," %5.2f",tau);
        }
      }
      fprintf(fp,"\n");
    }
    else
    {
      for(i=0;i<nsta;i++)
      {
        tau = -9.99;
        fprintf(fp," %5.2f",tau);
      }
      fprintf(fp,"\n");
    }
  }

  fclose(fp);

/*End main routine.*/
}


/*
* Enter kx, ky in s/km, output will be time shift in seconds
* If, for instance, kx and ky are positive, meaning signal is coming from
* the northeast, then for rx,ry both positive the signal is advanced at a
* station with coordinates (rx,ry), relative to that at recorded at (0,0).
* Therefore, a positive t means delay the signal, or shift it forward in time.
* The "delay" is an empirical (or estimated) delay at each station due
* to inhomogeneity (it may equal zero).  It is defined such that a positive
* delay means the signal is later than expected, relative to the plane wave; 
* and so t is adjusted downward.  Positive delay means the signal arrives 
* after it is expected; negative delay means it arrives before.
*/
double get_shift(double kx,double ky,double rx,double ry,double delay)
{
        double t;

        t=kx*rx + ky*ry;     /*uniform propagation shift*/
        t=t-delay;                   /*inhomogeneity terms*/

        return(t);
}


int align_signals(double del,double kx,double ky)
{
	int i,j,sind;
	double tshift;
        float *sout;
        int iret;
        double mean,stdv;

        sout = malloc(spts*sizeof(float));

	for(j=0;j<nsta;j++)
        {
            for(i=0;i<spts;i++) sout[i] = 0.0;

            tshift=get_shift(kx,ky,rx[j],ry[j],delay[j]);
            sind=(int)((-tshift)/del)+1;

            if(sind<0)
            {
                for(i=-sind;i<spts;i++)
                {
                    sout[i]+=s[j][sind+i];
                }
            } 
            if(sind>=0)
            {
                for(i=0;i<(spts-sind-1);i++)
                {
                    sout[i]+=s[j][sind+i];
                }
            } 
/*          Put sout back into signal vector to align signal.*/
            for(i=0;i<spts;i++)
            {
              s[j][i] = (double)sout[i];
            }
        }

	return 0;
}

int get_stat(float *dat,int ndat,double *mean,double *stdv)
{
	int i;
	double sum;

	sum=0.0;
	for(i=0;i<ndat;i++)
        {
	    sum+=dat[i];
	}

	*mean=sum/(double)ndat;

	sum=0.0;
	for(i=0;i<ndat;i++)
        {
	    sum+=(*mean-dat[i])*(*mean-dat[i]);
	}

	*stdv=sqrt(sum/(double)(ndat-1));

	return(0);
}
