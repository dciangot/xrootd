/******************************************************************************/
/*                                                                            */
/*                        X r d P r o t L o a d . c c                         */
/*                                                                            */
/* (c) 2006 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC02-76-SFO0515 with the Department of Energy              */
/*                                                                            */
/* This file is part of the XRootD software suite.                            */
/*                                                                            */
/* XRootD is free software: you can redistribute it and/or modify it under    */
/* the terms of the GNU Lesser General Public License as published by the     */
/* Free Software Foundation, either version 3 of the License, or (at your     */
/* option) any later version.                                                 */
/*                                                                            */
/* XRootD is distributed in the hope that it will be useful, but WITHOUT      */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public       */
/* License for more details.                                                  */
/*                                                                            */
/* You should have received a copy of the GNU Lesser General Public License   */
/* along with XRootD in a file called COPYING.LESSER (LGPL license) and file  */
/* COPYING (GPL license).  If not, see <http://www.gnu.org/licenses/>.        */
/*                                                                            */
/* The copyright holder's institutional names and contributor's names may not */
/* be used to endorse or promote products derived from this software without  */
/* specific prior written permission of the institution or contributor.       */
/******************************************************************************/

#include "XrdOuc/XrdOucPinLoader.hh"
#include "XrdSys/XrdSysError.hh"

#include "Xrd/XrdLink.hh"
#include "Xrd/XrdProtLoad.hh"
#include "Xrd/XrdTrace.hh"

#include "XrdVersion.hh"

/******************************************************************************/
/*                        G l o b a l   O b j e c t s                         */
/******************************************************************************/

XrdProtocol *XrdProtLoad::Protocol[ProtoMax] = {0};
char        *XrdProtLoad::ProtName[ProtoMax] = {0};
int          XrdProtLoad::ProtPort[ProtoMax] = {0};
bool         XrdProtLoad::ProtoTLS[ProtoMax] = {false};

int          XrdProtLoad::ProtoCnt = 0;

namespace
{
char            *liblist[XrdProtLoad::ProtoMax];
XrdOucPinLoader *libhndl[XrdProtLoad::ProtoMax];
const char      *TraceID = "ProtLoad";
int              libcnt = 0;
}

namespace XrdGlobal
{
extern XrdSysError Log;
}
using namespace XrdGlobal;

/******************************************************************************/
/*            C o n s t r u c t o r   a n d   D e s t r u c t o r             */
/******************************************************************************/
  
 XrdProtLoad::XrdProtLoad(int port) :
              XrdProtocol("protocol loader"), myPort(port)
{
   int j = 0;
   bool hastls = false;

// Extract out the protocols associated with this port
//
   for (int i = 0; i < ProtoCnt; i++)
       {if (myPort == ProtPort[i])
           {if (ProtoTLS[i]) hastls = true;
               else myProt[j++] = i;
           }
       }

// Setup to handle tls protocols
//
   if (hastls)
      {myProt[j++] = -1;
       for (int i = 0; i < ProtoCnt; i++)
           {if (myPort == ProtPort[i]) myProt[j++] = i;}
      }

// Put in an end marker
//
   myProt[j] = -2;
}

 XrdProtLoad::~XrdProtLoad() {}
 
/******************************************************************************/
/*                                  L o a d                                   */
/******************************************************************************/

int XrdProtLoad::Load(const char *lname, const char *pname,
                      char *parms, XrdProtocol_Config *pi, bool istls)
{
   XrdProtocol *xp;
   int port = pi->Port;

// Trace this load if so wanted
//
   TRACE(DEBUG, "getting protocol object " <<pname);

// First check to see that we haven't exceeded our protocol count
//
   if (ProtoCnt >= ProtoMax)
      {Log.Emsg("Protocol", "Too many protocols have been defined.");
       return 0;
      }

// Obtain an instance of this protocol
//
   xp = getProtocol(lname, pname, parms, pi);
   if (!xp) {Log.Emsg("Protocol","Protocol", pname, "could not be loaded");
             return 0;
            }

// Add protocol to our table of protocols.
//
   ProtName[ProtoCnt] = strdup(pname);
   ProtPort[ProtoCnt] = port;
   Protocol[ProtoCnt] = xp;
   ProtoTLS[ProtoCnt] = istls;
   ProtoCnt++;
   return 1;
}
  
/******************************************************************************/
/*                                  P o r t                                   */
/******************************************************************************/

int XrdProtLoad::Port(const char *lname, const char *pname,
                      char *parms, XrdProtocol_Config *pi)
{
   int port;

// Obtain the port number to be used by this protocol
//
   port = getProtocolPort(lname, pname, parms, pi);

// Trace this call if so wanted
//
   TRACE(DEBUG, "protocol " <<pname <<" wants to use port " <<port);

// Make sure we can use the port
//
   if (port < 0) Log.Emsg("Protocol","Protocol", pname,
                             "port number could not be determined");
   return port;
}
  
/******************************************************************************/
/*                               P r o c e s s                                */
/******************************************************************************/
  
int XrdProtLoad::Process(XrdLink *lp)
{
     XrdProtocol *pp = 0;
     signed char *pVec = myProt;
     int i = 0;

// Try to find a protocol match for this connection
//
   while(*pVec != -2)
        {if (*pVec == -1)
            {if (!(lp->setTLS(true)))
                {lp->setEtext("TLS negotiation failed.");
                 return -1;
                }
            } else {i = *pVec;
                    if ((pp = Protocol[i]->Match(lp))) break;
                       else if (lp->isFlawed()) return -1;
                   }
         pVec++;
        }

// Verify that we have an actual protocol
//
   if (!pp) {lp->setEtext("matching protocol not found"); return -1;}

// Now attach the new protocol object to the link
//
   lp->setProtocol(pp);
   lp->setProtName(ProtName[i]);

// Trace this load if so wanted
//
   TRACE(DEBUG, "matched port " <<myPort <<" protocol " <<ProtName[i]);

// Activate this link
//
   if (!lp->Activate()) {lp->setEtext("activation failed"); return -1;}

// Take a short-cut and process the initial request as a sticky request
//
   return pp->Process(lp);
}
 
/******************************************************************************/
/*                               R e c y c l e                                */
/******************************************************************************/
  
void XrdProtLoad::Recycle(XrdLink *lp, int ctime, const char *reason)
{

// Document non-protocol errors
//
   if (lp && reason)
      Log.Emsg("Protocol", lp->ID, "terminated", reason);
}

/******************************************************************************/
/*                            S t a t i s t i c s                             */
/******************************************************************************/

int XrdProtLoad::Statistics(char *buff, int blen, int do_sync)
{
    int i, k, totlen = 0;

    for (i = 0; i < ProtoCnt && (blen > 0 || !buff); i++)
        {k = Protocol[i]->Stats(buff, blen, do_sync);
         totlen += k; buff += k; blen -= k;
        }

    return totlen;
}

/******************************************************************************/
/*                       P r i v a t e   M e t h o d s                        */
/******************************************************************************/
/******************************************************************************/
/*                           g e t P r o t o c o l                            */
/******************************************************************************/

extern "C" XrdProtocol *XrdgetProtocol(const char *pname, char *parms,
                                       XrdProtocol_Config *pi);
  
XrdProtocol *XrdProtLoad::getProtocol(const char *lname,
                                      const char *pname,
                                            char *parms,
                              XrdProtocol_Config *pi)
{
   XrdProtocol *(*ep)(const char *, char *, XrdProtocol_Config *);
   const char *xname = (lname ? lname : "");
   void *epvoid;
   int i;

// If this is a builtin protocol getthe protocol object directly
//
   if (!lname) return XrdgetProtocol(pname, parms, pi);

// Find the matching library. It must be here because getPort was already called
//
   for (i = 0; i < libcnt; i++) if (!strcmp(xname, liblist[i])) break;
   if (i >= libcnt)
      {Log.Emsg("Protocol", pname, "was lost during loading", lname);
       return 0;
      }

// Obtain an instance of the protocol object and return it
//
   if (!(epvoid = libhndl[i]->Resolve("XrdgetProtocol"))) return 0;
   ep = (XrdProtocol *(*)(const char*,char*,XrdProtocol_Config*))epvoid;
   return ep(pname, parms, pi);
}

/******************************************************************************/
/*                       g e t P r o t o c o l P o r t                        */
/******************************************************************************/

   extern "C" int XrdgetProtocolPort(const char *pname, char *parms,
                                     XrdProtocol_Config *pi);
  
int XrdProtLoad::getProtocolPort(const char *lname,
                                 const char *pname,
                                       char *parms,
                         XrdProtocol_Config *pi)
{
   static XrdVERSIONINFODEF(myVer, xrd, XrdVNUMBER, XrdVERSION);
   const char *xname = (lname ? lname : "");
   int (*ep)(const char *, char *, XrdProtocol_Config *);
   void *epvoid;
   int i;

// If this is for the builtin protocol then get the port directly
//
   if (!lname) return XrdgetProtocolPort(pname, parms, pi);

// See if the library is already opened, if not open it
//
   for (i = 0; i < libcnt; i++) if (!strcmp(xname, liblist[i])) break;
   if (i >= libcnt)
      {if (libcnt >= ProtoMax)
          {Log.Emsg("Protocol", "Too many protocols have been defined.");
           return -1;
          }
       if (!(libhndl[i] = new XrdOucPinLoader(&Log,&myVer,"protocol",lname)))
          return -1;
       liblist[i] = strdup(xname);
       libcnt++;
      }

// Get the port number to be used
//
   if (!(epvoid = libhndl[i]->Resolve("XrdgetProtocolPort", 2)))
      return (pi->Port < 0 ? 0 : pi->Port);
   ep = (int (*)(const char*,char*,XrdProtocol_Config*))epvoid;
   return ep(pname, parms, pi);
}
