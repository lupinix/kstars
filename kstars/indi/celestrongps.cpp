#if 0
    Celestron GPS
    Copyright (C) 2003 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

#include "celestronprotocol.h"
#include "celestrongps.h"

#define mydev 		"Celestron GPS"

/* Enable to log debug statements 
#define CELESTRON_DEBUG	1
*/

CelestronGPS *telescope = NULL;


/* There is _one_ binary for all LX200 drivers, but each binary is renamed
** to its device name (i.e. lx200gps, lx200_16..etc). The main function will
** fetch from std args the binary name and ISInit will create the apporpiate
** device afterwards. If the binary name does not match any known devices,
** we simply create a generic device
*/
extern char* me;

#define COMM_GROUP	"Communication"
#define BASIC_GROUP	"Main Control"
#define MOVE_GROUP	"Movement Control"

static void ISPoll(void *);

/*INDI controls */
static ISwitch PowerS[]          = {{"CONNECT" , "Connect" , ISS_OFF, 0, 0},{"DISCONNECT", "Disconnect", ISS_ON, 0, 0}};
static ISwitch SlewModeS[]       = {{"Slew", "", ISS_ON, 0, 0}, {"Find", "", ISS_OFF, 0, 0}, {"Centering", "", ISS_OFF, 0, 0}, {"Guide", "", ISS_OFF, 0, 0}};
static ISwitch OnCoordSetS[]     = {{"SLEW", "Slew", ISS_ON, 0 , 0}, {"TRACK", "Track", ISS_OFF, 0, 0}, {"SYNC", "Sync", ISS_OFF, 0, 0}};
static ISwitch abortSlewS[]      = {{"ABORT", "Abort", ISS_OFF, 0, 0}};

/* equatorial position */
static INumber eq[] = {
    {"RA",  "RA  H:M:S", "%10.6m",  0., 24., 0., 0., 0, 0, 0},
    {"DEC", "Dec D:M:S", "%10.6m", -90., 90., 0., 0., 0, 0, 0},
};

static INumberVectorProperty eqNum = {
    mydev, "EQUATORIAL_EOD_COORD", "Equatorial JNow", BASIC_GROUP, IP_RW, 0, IPS_IDLE,
    eq, NARRAY(eq), "", 0};

/* Tracking precision */
INumber trackingPrecisionN[] = {
    {"TrackRA",  "RA (arcmin)", "%10.6m",  0., 60., 1., 3.0, 0, 0, 0},
    {"TrackDEC", "Dec (arcmin)", "%10.6m", 0., 60., 1., 3.0, 0, 0, 0},
};
static INumberVectorProperty trackingPrecisionNP = {mydev, "Tracking Precision", "", MOVE_GROUP, IP_RW, 0, IPS_IDLE, trackingPrecisionN, NARRAY(trackingPrecisionN), "", 0};

/* Slew precision */
INumber slewPrecisionN[] = {
    {"SlewRA",  "RA (arcmin)", "%10.6m",  0., 60., 1., 3.0, 0, 0, 0},
    {"SlewDEC", "Dec (arcmin)", "%10.6m", 0., 60., 1., 3.0, 0, 0, 0},
};
static INumberVectorProperty slewPrecisionNP = {mydev, "Slew Precision", "", MOVE_GROUP, IP_RW, 0, IPS_IDLE, slewPrecisionN, NARRAY(slewPrecisionN), "", 0};

/* Fundamental group */
static ISwitchVectorProperty PowerSw	= { mydev, "CONNECTION" , "Connection", COMM_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, PowerS, NARRAY(PowerS), "", 0};
static IText PortT[]			= {{"PORT", "Port", 0, 0, 0, 0}};
static ITextVectorProperty Port		= { mydev, "DEVICE_PORT", "Ports", COMM_GROUP, IP_RW, 0, IPS_IDLE, PortT, NARRAY(PortT), "", 0};

/* Movement group */
static ISwitchVectorProperty OnCoordSetSw    = { mydev, "ON_COORD_SET", "On Set", BASIC_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, OnCoordSetS, NARRAY(OnCoordSetS), "", 0};
static ISwitchVectorProperty abortSlewSw     = { mydev, "ABORT_MOTION", "Abort Slew/Track", BASIC_GROUP, IP_RW, ISR_ATMOST1, 0, IPS_IDLE, abortSlewS, NARRAY(abortSlewS), "", 0};
static ISwitchVectorProperty SlewModeSw      = { mydev, "Slew rate", "", MOVE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, SlewModeS, NARRAY(SlewModeS), "", 0};

/* Movement (Arrow keys on handset). North/South */
static ISwitch MovementNSS[]       = {{"N", "North", ISS_OFF, 0, 0}, {"S", "South", ISS_OFF, 0, 0}};

static ISwitchVectorProperty MovementNSSP      = { mydev, "MOVEMENT_NS", "North/South", MOVE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, MovementNSS, NARRAY(MovementNSS), "", 0};

/* Movement (Arrow keys on handset). West/East */
static ISwitch MovementWES[]       = {{"W", "West", ISS_OFF, 0, 0}, {"E", "East", ISS_OFF, 0, 0}};

static ISwitchVectorProperty MovementWESP      = { mydev, "MOVEMENT_WE", "West/East", MOVE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, MovementWES, NARRAY(MovementWES), "", 0};


/* send client definitions of all properties */
void ISInit()
{
  static int isInit=0;

 if (isInit)
  return;

 isInit = 1;

  PortT[0].text = strcpy(new char[32], "/dev/ttyS0");
  
  telescope = new CelestronGPS();

  IEAddTimer (POLLMS, ISPoll, NULL);
}

void ISGetProperties (const char *dev)
{ ISInit(); telescope->ISGetProperties(dev);}
void ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{ ISInit(); telescope->ISNewSwitch(dev, name, states, names, n);}
void ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{ ISInit(); telescope->ISNewText(dev, name, texts, names, n);}
void ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{ ISInit(); telescope->ISNewNumber(dev, name, values, names, n);}
void ISPoll (void *p) { telescope->ISPoll(); IEAddTimer (POLLMS, ISPoll, NULL); p=p;}
void ISNewBLOB (const char */*dev*/, const char */*name*/, int */*sizes[]*/, char **/*blobs[]*/, char **/*formats[]*/, char **/*names[]*/, int /*n*/)
{}

/**************************************************
*** LX200 Generic Implementation
***************************************************/

CelestronGPS::CelestronGPS()
{

   targetRA  = lastRA = 0;
   targetDEC = lastDEC = 0;
   currentSet   = 0;
   lastSet      = -1;

   JD = 0;

   // Children call parent routines, this is the default
   IDLog("initilizaing from Celeston GPS device...\n");

}

void CelestronGPS::ISGetProperties(const char *dev)
{

 if (dev && strcmp (mydev, dev))
    return;

  // COMM_GROUP
  IDDefSwitch (&PowerSw, NULL);
  IDDefText   (&Port, NULL);
  
  // BASIC_GROUP
  IDDefNumber (&eqNum, NULL);
  IDDefSwitch (&OnCoordSetSw, NULL);
  IDDefSwitch (&abortSlewSw, NULL);
  IDDefSwitch (&SlewModeSw, NULL);

  // Movement group
  IDDefSwitch (&MovementNSSP, NULL);
  IDDefSwitch (&MovementWESP, NULL);
  IDDefNumber (&trackingPrecisionNP, NULL);
  IDDefNumber (&slewPrecisionNP, NULL);
  
  /* Send the basic data to the new client if the previous client(s) are already connected. */		
  if (PowerSw.s == IPS_OK)
        getBasicData();

}

void CelestronGPS::ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
        IText *tp;

	// suppress warning
	n=n;
	// ignore if not ours //
	if (strcmp (dev, mydev))
	    return;

	if (!strcmp(name, Port.name) )
	{
	  Port.s = IPS_OK;

	  tp = IUFindText( &Port, names[0] );
	  if (!tp)
	   return;

	  tp->text = new char[strlen(texts[0])+1];
	  strcpy(tp->text, texts[0]);
	  IDSetText (&Port, NULL);
	  return;
	}
}

int CelestronGPS::handleCoordSet()
{

  int i=0;
  char RAStr[32], DecStr[32];

  switch (currentSet)
  {

    // Slew
    case 0:
          lastSet = 0;
	  if (eqNum.s == IPS_BUSY)
	  {
	     StopNSEW();
	     // sleep for 500 mseconds
	     usleep(500000);
	  }

	  if ((i = SlewToCoords(targetRA, targetDEC)))
	  {
	    slewError(i);
	    return (-1);
	  }

	  eqNum.s = IPS_BUSY;
	  fs_sexa(RAStr, targetRA, 2, 3600);
	  fs_sexa(DecStr, targetDEC, 2, 3600);
	  IDSetNumber(&eqNum, "Slewing to JNOW RA %s - DEC %s", RAStr, DecStr);
	  IDLog("Slewing to JNOW RA %s - DEC %s", RAStr, DecStr);
	  break;


  // Track
  case 1: 
          if (eqNum.s == IPS_BUSY)
	  {
	      StopNSEW();
	     // sleep for 500 mseconds
	     usleep(500000);
	  }

	  if ( (fabs ( targetRA - currentRA ) >= (trackingPrecisionN[0].value/(15.0*60.0))) ||
 	       (fabs (targetDEC - currentDEC) >= (trackingPrecisionN[1].value)/60.0))
	  {

		#ifdef CELESTRON_DEBUG
	        IDLog("Exceeded Tracking threshold, will attempt to slew to the new target.\n");
		IDLog("targetRA is %g, currentRA is %g\n", targetRA, currentRA);
	        IDLog("targetDEC is %g, currentDEC is %g\n*************************\n", targetDEC, currentDEC);
		#endif

          	if (( i =  SlewToCoords(targetRA, targetDEC)))
	  	{
	    		slewError(i);
	    		return (-1);
	  	}
		
		fs_sexa(RAStr, targetRA, 2, 3600);
	        fs_sexa(DecStr, targetDEC, 2, 3600);
		eqNum.s = IPS_BUSY;
		IDSetNumber(&eqNum, "Slewing to JNow RA %s - DEC %s", RAStr, DecStr);
		IDLog("Slewing to JNOW RA %s - DEC %s", RAStr, DecStr);
	  }
	  else
	  {
	    #ifdef CELESTRON_DEBUG
	    IDLog("Tracking called, but tracking threshold not reached yet.\n");
	    #endif
	    eqNum.s = IPS_OK;
	    eqNum.np[0].value = currentRA;
	    eqNum.np[1].value = currentDEC;
            if (lastSet != 1)
	      IDSetNumber(&eqNum, "Tracking...");
	    else
	      IDSetNumber(&eqNum, NULL);
	  }
	  lastSet = 1;
      break;
      
    // Sync
    case 2:
          lastSet = 2;
	  OnCoordSetSw.s = IPS_OK;
	  SyncToCoords(targetRA, targetDEC);
          eqNum.s = IPS_OK;
   	  IDSetNumber(&eqNum, "Synchronization successful.");
	  break;
   }

   return (0);

}

void CelestronGPS::ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{
        double newRA=0, newDEC=0;

        // ignore if not ours //
	if (strcmp (dev, mydev))
	    return;

	struct tm *tp;
	time_t t;

	time (&t);
	tp = gmtime (&t);

        if (!strcmp (name, trackingPrecisionNP.name))
	{
		if (!IUUpdateNumbers(&trackingPrecisionNP, values, names, n))
		{
			trackingPrecisionNP.s = IPS_OK;
			IDSetNumber(&trackingPrecisionNP, NULL);
			return;
		}
		
		trackingPrecisionNP.s = IPS_ALERT;
		IDSetNumber(&trackingPrecisionNP, "unknown error while setting tracking precision");
		return;
	}

	if (!strcmp(name, slewPrecisionNP.name))
	{
		IUUpdateNumbers(&slewPrecisionNP, values, names, n);
		{
			slewPrecisionNP.s = IPS_OK;
			IDSetNumber(&slewPrecisionNP, NULL);
			return;
		}
		
		slewPrecisionNP.s = IPS_ALERT;
		IDSetNumber(&slewPrecisionNP, "unknown error while setting slew precision");
		return;
	}

	if (!strcmp (name, eqNum.name))
	{
	  int i=0, nset=0;

	  if (checkPower(&eqNum))
	   return;

	    for (nset = i = 0; i < n; i++)
	    {
		INumber *eqp = IUFindNumber (&eqNum, names[i]);
		if (eqp == &eq[0])
		{
                    newRA = values[i];
		    nset += newRA >= 0 && newRA <= 24.0;
		} else if (eqp == &eq[1])
		{
		    newDEC = values[i];
		    nset += newDEC >= -90.0 && newDEC <= 90.0;
		}
	    }

	  if (nset == 2)
	  {
	   //eqNum.s = IPS_BUSY;

	   tp->tm_mon   += 1;
	   tp->tm_year  += 1900;

	   // update JD
           JD = UTtoJD(tp);

	   #ifdef CELESTRON_DEBUG
	   IDLog("We recevined JNOW RA %f - DEC %f\n", newRA, newDEC);;
	   #endif
	   
	   targetRA  = newRA;
	   targetDEC = newDEC;
	       
	   if (MovementNSSP.s == IPS_BUSY || MovementWESP.s == IPS_BUSY)
	   {
	   	IUResetSwitches(&MovementNSSP);
		IUResetSwitches(&MovementWESP);
		MovementNSSP.s = MovementWESP.s = IPS_IDLE;
		IDSetSwitch(&MovementNSSP, NULL);
		IDSetSwitch(&MovementWESP, NULL);
	   }
	   
	       if (handleCoordSet())
	       {
	        eqNum.s = IPS_IDLE;
	    	IDSetNumber(&eqNum, NULL);
	       }
	    }
	    else
	    {
		eqNum.s = IPS_IDLE;
		IDSetNumber(&eqNum, "RA or Dec missing or invalid.");
	    }

	    return;
	 }
}

void CelestronGPS::ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{

        int index;

	// Suppress warning
	names = names;

	//IDLog("in new Switch with Device= %s and Property= %s and #%d items\n", dev, name,n);
	//IDLog("SolarSw name is %s\n", SolarSw.name);

	// ignore if not ours //
	if (strcmp (dev, mydev))
	    return;

	// FIRST Switch ALWAYS for power
	if (!strcmp (name, PowerSw.name))
	{
	 if (IUUpdateSwitches(&PowerSw, states, names, n) < 0) return;
   	 connectTelescope();
	 return;
	}

	if (!strcmp(name, OnCoordSetSw.name))
	{
  	  if (checkPower(&OnCoordSetSw))
	   return;

	  
	  if (IUUpdateSwitches(&OnCoordSetSw, states, names, n) < 0) return;
	  currentSet = getOnSwitch(&OnCoordSetSw);
	}
	
	// Abort Slew
	if (!strcmp (name, abortSlewSw.name))
	{
	  if (checkPower(&abortSlewSw))
	  {
	    abortSlewSw.s = IPS_IDLE;
	    IDSetSwitch(&abortSlewSw, NULL);
	    return;
	  }
	  
	  IUResetSwitches(&abortSlewSw);
	  StopNSEW();

	    if (eqNum.s == IPS_BUSY)
	    {
		abortSlewSw.s = IPS_OK;
		eqNum.s       = IPS_IDLE;
		IDSetSwitch(&abortSlewSw, "Slew aborted.");
		IDSetNumber(&eqNum, NULL);
            }
	    else if (MovementNSSP.s == IPS_BUSY || MovementWESP.s == IPS_BUSY)
	    {
		MovementNSSP.s  = MovementWESP.s =  IPS_IDLE; 
	
		abortSlewSw.s = IPS_OK;		
		eqNum.s       = IPS_IDLE;
		IUResetSwitches(&MovementNSSP);
		IUResetSwitches(&MovementWESP);
		IUResetSwitches(&abortSlewSw);

		IDSetSwitch(&abortSlewSw, "Slew aborted.");
		IDSetSwitch(&MovementNSSP, NULL);
		IDSetSwitch(&MovementWESP, NULL);
		IDSetNumber(&eqNum, NULL);
	    }
	    else
	    {
	        abortSlewSw.s = IPS_IDLE;
	        IDSetSwitch(&abortSlewSw, NULL);
	    }

	    return;
	}

	// Slew mode
	if (!strcmp (name, SlewModeSw.name))
	{
	  if (checkPower(&SlewModeSw))
	   return;

	  IUResetSwitches(&SlewModeSw);
	  IUUpdateSwitches(&SlewModeSw, states, names, n);
	  index = getOnSwitch(&SlewModeSw);
	  SetRate(index);
          
	  SlewModeSw.s = IPS_OK;
	  IDSetSwitch(&SlewModeSw, NULL);
	  return;
	}

	// Movement (North/South)
	if (!strcmp (name, MovementNSSP.name))
	{
	  if (checkPower(&MovementNSSP))
	   return;

	 int last_move=-1;
         int current_move = -1;

	// -1 means all off previously
	 last_move = getOnSwitch(&MovementNSSP);

	 if (IUUpdateSwitches(&MovementNSSP, states, names, n) < 0)
		return;

	current_move = getOnSwitch(&SlewModeSw);

	// Previosuly active switch clicked again, so let's stop.
	if (current_move == last_move)
	{
		StopSlew((current_move == 0) ? NORTH : SOUTH);
		IUResetSwitches(&MovementNSSP);
	    	MovementNSSP.s = IPS_IDLE;
	    	IDSetSwitch(&MovementNSSP, NULL);
		return;
	}

	#ifdef CELESTRON_DEBUG
        IDLog("Current Move: %d - Previous Move: %d\n", current_move, last_move);
	#endif

	// 0 (North) or 1 (South)
	last_move      = current_move;

	// Correction for Celestron Driver: North 0 - South 3
	current_move = (current_move == 0) ? NORTH : SOUTH;

	StartSlew(current_move);
	
	  MovementNSSP.s = IPS_BUSY;
	  IDSetSwitch(&MovementNSSP, "Moving toward %s", (current_move == NORTH) ? "North" : "South");
	  return;
	}

	// Movement (West/East)
	if (!strcmp (name, MovementWESP.name))
	{
	  if (checkPower(&MovementWESP))
	   return;

	 int last_move=-1;
         int current_move = -1;

	// -1 means all off previously
	 last_move = getOnSwitch(&MovementWESP);

	 if (IUUpdateSwitches(&MovementWESP, states, names, n) < 0)
		return;

	current_move = getOnSwitch(&SlewModeSw);

	// Previosuly active switch clicked again, so let's stop.
	if (current_move == last_move)
	{
		StopSlew((current_move ==0) ? WEST : EAST);
		IUResetSwitches(&MovementWESP);
	    	MovementWESP.s = IPS_IDLE;
	    	IDSetSwitch(&MovementWESP, NULL);
		return;
	}

	#ifdef CELESTRON_DEBUG
        IDLog("Current Move: %d - Previous Move: %d\n", current_move, last_move);
	#endif

	// 0 (West) or 1 (East)
	last_move      = current_move;

	// Correction for Celestron Driver: West 1 - East 2
	current_move = (current_move == 0) ? WEST : EAST;

	StartSlew(current_move);
	
	  MovementWESP.s = IPS_BUSY;
	  IDSetSwitch(&MovementWESP, "Moving toward %s", (current_move == WEST) ? "West" : "East");
	  return;
	}
}


int CelestronGPS::getOnSwitch(ISwitchVectorProperty *sp)
{
 for (int i=0; i < sp->nsp ; i++)
     if (sp->sp[i].s == ISS_ON)
      return i;

 return -1;
}


int CelestronGPS::checkPower(ISwitchVectorProperty *sp)
{
  if (PowerSw.s != IPS_OK)
  {
    if (!strcmp(sp->label, ""))
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", sp->name);
    else
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", sp->label);
       
    sp->s = IPS_IDLE;
    IDSetSwitch(sp, NULL);
    return -1;
  }

  return 0;
}

int CelestronGPS::checkPower(INumberVectorProperty *np)
{
  if (PowerSw.s != IPS_OK)
  {
    if (!strcmp(np->label, ""))
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", np->name);
    else
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", np->label);
       
    np->s = IPS_IDLE;
    IDSetNumber(np, NULL);
    return -1;
  }
  return 0;
}

int CelestronGPS::checkPower(ITextVectorProperty *tp)
{

  if (PowerSw.s != IPS_OK)
  {
    if (!strcmp(tp->label, ""))
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", tp->name);
    else
       IDMessage (mydev, "Cannot change property %s while the telescope is offline.", tp->label);
       
    tp->s = IPS_IDLE;
    IDSetText(tp, NULL);
    return -1;
  }

  return 0;

}

void CelestronGPS::ISPoll()
{
       double dx, dy;
       double currentRA, currentDEC;
       int status;

	switch (eqNum.s)
	{
	case IPS_IDLE:
	if (PowerSw.s != IPS_OK)
	 break;
	currentRA = GetRA();
	currentDEC = GetDec();

        if ( fabs (currentRA - lastRA) > 0.01 || fabs (currentDEC - lastDEC) > 0.01)
	{
	        eqNum.np[0].value = currentRA;
		eqNum.np[1].value = currentDEC;
		lastRA  = currentRA;
		lastDEC = currentDEC;
		IDSetNumber (&eqNum, NULL);

	}
        break;

        case IPS_BUSY:
	    currentRA = GetRA();
	    currentDEC = GetDec();
	    dx = targetRA - currentRA;
	    dy = targetDEC - currentDEC;

            #ifdef CELESTRON_DEBUG
	    IDLog("targetRA is %f, currentRA is %f\n", (float) targetRA, (float) currentRA);
	    IDLog("targetDEC is %f, currentDEC is %f\n****************************\n", (float) targetDEC, (float) currentDEC);
	    #endif

	    eqNum.np[0].value = currentRA;
	    eqNum.np[1].value = currentDEC;

	    status = CheckCoords(targetRA, targetDEC, slewPrecisionN[0].value/(15.0*60.0) , slewPrecisionN[1].value/60.0);

	    // Wait until acknowledged or within 3.6', change as desired.
	    switch (status)
	    {
	    case 0:		/* goto in progress */
		IDSetNumber (&eqNum, NULL);
		break;
	    case 1:		/* goto complete within tolerance */
	    case 2:		/* goto complete but outside tolerance */
		currentRA = targetRA;
		currentDEC = targetDEC;

		/*apparentCoord( JD, (double) J2000, &currentRA, &currentDEC);*/

		eqNum.np[0].value = currentRA;
		eqNum.np[1].value = currentDEC;

		eqNum.s = IPS_OK;

		if (currentSet == 0)
		{
		  IUResetSwitches(&OnCoordSetSw);
		  OnCoordSetSw.sp[0].s = ISS_ON;
		  IDSetNumber (&eqNum, "Slew is complete");
		}
		else
		{
		  IUResetSwitches(&OnCoordSetSw);
		  OnCoordSetSw.sp[1].s = ISS_ON;
		  IDSetNumber (&eqNum, "Slew is complete. Tracking...");
		}
		
		IDSetSwitch (&OnCoordSetSw, NULL);
		break;
	    }   
	    break;

	case IPS_OK:
	if (PowerSw.s != IPS_OK)
	 break;
	currentRA = GetRA();
	currentDEC = GetDec();

        if ( fabs (currentRA - lastRA) > 0.01 || fabs (currentDEC - lastDEC) > 0.01)
	{

		eqNum.np[0].value = currentRA;
		eqNum.np[1].value = currentDEC;
		lastRA  = currentRA;
		lastDEC = currentDEC;
		IDSetNumber (&eqNum, NULL);

	}
        break;


	case IPS_ALERT:
	    break;
	}

	switch (MovementNSSP.s)
	{
	  case IPS_IDLE:
	   break;
	 case IPS_BUSY:
	     currentRA = GetRA();
	     currentDEC = GetDec();

	     eqNum.np[0].value = currentRA;
	     eqNum.np[1].value = currentDEC;

	     IDSetNumber (&eqNum, NULL);

	     break;
	 case IPS_OK:
	   break;
	 case IPS_ALERT:
	   break;
	 }

	switch (MovementWESP.s)
	{
	  case IPS_IDLE:
	   break;
	 case IPS_BUSY:
	     currentRA = GetRA();
	     currentDEC = GetDec();

	     eqNum.np[0].value = currentRA;
	     eqNum.np[1].value = currentDEC;

	     IDSetNumber (&eqNum, NULL);

	     break;
	 case IPS_OK:
	   break;
	 case IPS_ALERT:
	   break;
	 }

}

void CelestronGPS::getBasicData()
{

  targetRA = GetRA();
  targetDEC = GetDec();

  eqNum.np[0].value = targetRA;
  eqNum.np[1].value = targetDEC;

  IDSetNumber(&eqNum, NULL);

}

void CelestronGPS::connectTelescope()
{

     switch (PowerSw.sp[0].s)
     {
      case ISS_ON:

         if (ConnectTel(Port.tp[0].text) < 0)
	 {
	   PowerS[0].s = ISS_OFF;
	   PowerS[1].s = ISS_ON;
	   IDSetSwitch (&PowerSw, "Error connecting to port %s. Make sure you have BOTH write and read permission to the port.", Port.tp[0].text);
	   return;
	 }

	PowerSw.s = IPS_OK;
	IDSetSwitch (&PowerSw, "Telescope is online. Retrieving basic data...");
	getBasicData();
	break;

     case ISS_OFF:
         IDSetSwitch (&PowerSw, "Telescope is offline.");
	 IDLog("Telescope is offline.");
	 DisconnectTel();
	 break;

    }
}

void CelestronGPS::slewError(int slewCode)
{
    eqNum.s = IPS_IDLE;

    switch (slewCode)
    {
      case 1:
       IDSetNumber (&eqNum, "Invalid newDec in SlewToCoords");
       break;
      case 2:
       IDSetNumber (&eqNum, "RA count overflow in SlewToCoords");
       break;
      case 3:
       IDSetNumber (&eqNum, "Dec count overflow in SlewToCoords");
       break;
      case 4:
       IDSetNumber (&eqNum, "No acknowledgment from telescope after SlewToCoords");
       break;
      default:
       IDSetNumber (&eqNum, "Unknown error");
       break;
    }

}
