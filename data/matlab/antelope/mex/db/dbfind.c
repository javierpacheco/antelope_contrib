/* 
 * Matlab interface to Datascope package
 *
 * Kent Lindquist
 * Geophysical Institute
 * University of Alaska, Fairbanks
 * 1997
 */

#define USAGE "Error using ==> dbfind\n\n\
Usage: IRECORD = DBFIND ( DBPTR, EXPRESSION [,FIRST [,COUNT]] [,'backwards'] )\n"

#include "antelope_mex.h"

void mexFunction ( int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[] )
{
	Dbptr	db;
	int	nrecords;
	int	code = 0;
	int	first, backwards;
	char	*direction;
	Expression *expr;
	Dbvalue	result;
	int	type;
	double	scalar;
	int	*intp;
	char	*string;
	char	*expression;
	int	count = 0;
	int	rc;

	if( nrhs < 2 || nrhs > 5 )
	{
		antelope_mexUsageMsgTxt ( USAGE );
		return;
	}
        else if( ! get_dbptr( prhs[0], &db ) )
        {
                antelope_mexUsageMsgTxt ( USAGE );
		return;
        }
	else if( ! get_string( prhs[1], &expression ) )
	{
                antelope_mexUsageMsgTxt ( USAGE );
		return;
	}

	if( nrhs >= 3 && mxGetClassID( prhs[nrhs-1] ) == mxCHAR_CLASS )
	{
		if( ! get_string( prhs[nrhs-1], &direction ) )
		{
			mxFree( expression );
                	antelope_mexUsageMsgTxt ( USAGE );
			return;
		}
		else if( ! STREQ( direction, "backwards" ) )
		{
			mxFree( expression );
			mxFree( direction );
                	antelope_mexUsageMsgTxt ( USAGE );
			return;
		}
		else
		{
			mxFree( direction );
			backwards = 1;
		}
	}
	else
	{
		backwards = 0;
	}

	rc = dbquery ( db, dbRECORD_COUNT, &nrecords );
	antelope_mex_clear_register( 1 );
	if( rc == dbINVALID )
	{
		mxFree( expression );
		mexErrMsgTxt( "dbfind: dbquery failed" );
	}
	if( nrecords <= 0 )
	{
		mxFree( expression );
		mexErrMsgTxt( "dbfind: no records in view" );
	}


	if( backwards && nrhs == 3 )
	{
		/* We don't have to evaluate FIRST and COUNT arguments */
		/* Because they're not there */
		first = nrecords - 1;
	}
	else if( nrhs >= 3 )
	{
		if( ! get_scalar( prhs[2], &scalar ) )
		{
			mxFree( expression );
			antelope_mexUsageMsgTxt ( USAGE );
			return;
		}
		else
		{
			first = MAX( (int) scalar, 0 );
			first = MIN( first, nrecords - 1 );
		}

		/* Is there a COUNT argument: */
		if( ( backwards == 0 && nrhs == 4 ) ||
		    ( backwards == 1 && nrhs == 5 ) )
		{
			if( ! get_scalar( prhs[3], &scalar ) )
			{
				mxFree( expression );
				antelope_mexUsageMsgTxt ( USAGE );
			}
			else
			{
				count = (int) scalar;
				count -= 1;
				count = MAX( count, 0 );
			}
		}
	}
	else
	{
		first = 0;
	}

	if ( dbex_compile ( db, expression, &expr, 0 ) < 0 )
	{
		antelope_mex_clear_register( 1 );
		mxFree( expression );
		mexErrMsgTxt( "dbfind: expression failed to compile\n" );
	}
	else
	{
		antelope_mex_clear_register( 1 );
		mxFree( expression );
	}

	if ( backwards )
	{
		for ( db.record = first ; db.record >= 0 ; db.record-- )
		{
			code = dbex_eval ( db, expr, 1, &result );
			antelope_mex_clear_register( 1 );
			if ( code > 0 && count-- == 0 ) break;
		}
	}
	else
	{
		for ( db.record = first ; db.record < nrecords ; db.record++ )
		{
			code = dbex_eval ( db, expr, 1, &result );
			antelope_mex_clear_register( 1 );
			if ( code > 0 && count-- == 0 ) break;
		}
	}

	dbex_free ( expr );

	if( code < 0 )
	{
		mexErrMsgTxt( "dbfind: failed\n" );
	}
	else if ( code > 0 )
	{
		plhs[0] = CreateDouble( (double) db.record );
		if( plhs[0] == NULL )
		{
			mexErrMsgTxt( "dbfind: failed to create return value" );
		}
	}
	else 
	{
		plhs[0] = CreateDouble( (double) dbINVALID );
		if( plhs[0] == NULL )
		{
			mexErrMsgTxt( "dbfind: failed to create return value" );
		}
	}
}
