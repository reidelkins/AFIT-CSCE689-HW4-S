#include <iostream>
#include <exception>
#include "ReplServer.h"

const time_t secs_between_repl = 20;
const unsigned int max_servers = 10;

/*********************************************************************************************
 * ReplServer (constructor) - creates our ReplServer. Initializes:
 *
 *    verbosity - passes this value into QueueMgr and local, plus each connection
 *    _time_mult - how fast to run the simulation - 2.0 = 2x faster
 *    ip_addr - which ip address to bind the server to
 *    port - bind the server here
 *
 *********************************************************************************************/
ReplServer::ReplServer(DronePlotDB &plotdb, float time_mult)
                              :_queue(1),
                               _plotdb(plotdb),
                               _shutdown(false), 
                               _time_mult(time_mult),
                               _verbosity(1),
                               _ip_addr("127.0.0.1"),
                               _port(9999)
{
}

ReplServer::ReplServer(DronePlotDB &plotdb, const char *ip_addr, unsigned short port, float time_mult,
                                          unsigned int verbosity)
                                 :_queue(verbosity),
                                  _plotdb(plotdb),
                                  _shutdown(false), 
                                  _time_mult(time_mult), 
                                  _verbosity(verbosity),
                                  _ip_addr(ip_addr),
                                  _port(port)

{
}

ReplServer::~ReplServer() {

}


/**********************************************************************************************
 * getAdjustedTime - gets the time since the replication server started up in seconds, modified
 *                   by _time_mult to speed up or slow down
 **********************************************************************************************/

time_t ReplServer::getAdjustedTime() {
   return static_cast<time_t>((time(NULL) - _start_time) * _time_mult);
}

/**********************************************************************************************
 * replicate - the main function managing replication activities. Manages the QueueMgr and reads
 *             from the queue, deconflicting entries and populating the DronePlotDB object with
 *             replicated plot points.
 *
 *    Params:  ip_addr - the local IP address to bind the listening socket
 *             port - the port to bind the listening socket
 *             
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

void ReplServer::replicate(const char *ip_addr, unsigned short port) {
   _ip_addr = ip_addr;
   _port = port;
   replicate();
}

void ReplServer::replicate() {

   // Track when we started the server
   _start_time = time(NULL);
   _last_repl = 0;

   // Set up our queue's listening socket
   _queue.bindSvr(_ip_addr.c_str(), _port);
   _queue.listenSvr();

   if (_verbosity >= 2)
      std::cout << "Server bound to " << _ip_addr << ", port: " << _port << " and listening\n";

  
   // Replicate until we get the shutdown signal
   while (!_shutdown) {

      // Check for new connections, process existing connections, and populate the queue as applicable
      _queue.handleQueue();     

      // See if it's time to replicate and, if so, go through the database, identifying new plots
      // that have not been replicated yet and adding them to the queue for replication
      if (getAdjustedTime() - _last_repl > secs_between_repl) {

         queueNewPlots();
         _last_repl = getAdjustedTime();
      }
      
      // Check the queue for updates and pop them until the queue is empty. The pop command only returns
      // incoming replication information--outgoing replication in the queue gets turned into a TCPConn
      // object and automatically removed from the queue by pop
      std::string sid;
      std::vector<uint8_t> data;
      while (_queue.pop(sid, data)) {

         // Incoming replication--add it to this server's local database
         addReplDronePlots(data);         
      }       

      //CHANGE
      //Check for skews and replicates in database
      _plotdb.sortByTime();

      
      //get main node
      //any other nodes should go off the main node
      //find place where main node matches with another node, get time difference, for all enteries in the non main node, adjust time. Find place where other node
         //has entry with same location as one of the other nodes, then get time difference for that node
      int electedNode = 1;
      std::vector<time_t> skew(3, -16); //creates vector of size 3 with everything initialized to an impossible time for skew to be
      //first entry is diff between node 1 and 2, second entry is between node 1 and 3, third entry is between node 2 and 3
      
      //vector to hold
      for(auto i = _plotdb.begin(); i != _plotdb.end(); i++){
         for(auto j = std::next(_plotdb.begin(), 1); j != _plotdb.end(); j++){
            //can't do this because it won't get rid of all replicates
            //if(skewsFound != 2) {
            //if two entries have same location but were seen by different nodes, we can find the skew between those nodes
            //also this will delete any duplicates even after the skews are found
            if (i->latitude == j->latitude && i->longitude == j->longitude)  {
               if(i->node_id == j->node_id) {
                  _plotdb.erase(j);
               } else {
                  if(i->node_id == electedNode) {
                     if(j->node_id == 2) {
                        if(skew.at(0) == -16) {
                           skew.at(0) = i->timestamp - j->timestamp;
                        }
                        _plotdb.erase(j);
                     } else {// j node == 3 
                        if(skew.at(1) == -16) {
                           skew.at(1) = i->timestamp - j->timestamp;
                        }
                        _plotdb.erase(j);
                     }
                  } else if (j->node_id == electedNode) {
                     if(i->node_id == 2) {
                        if(skew.at(0) == -16) {
                           skew.at(0) = j->timestamp - i->timestamp;
                        }
                        _plotdb.erase(i);
                     } else {// i node == 3 
                        if(skew.at(1) == -16) {
                           skew.at(1) = j->timestamp - i->timestamp;
                        }
                        _plotdb.erase(i);
                     }
                  } else if (i->node_id == 2) {//this means j node is 3
                     if(skew.at(2) == -16) {
                        skew.at(2) = i->timestamp - j->timestamp;
                     }
                     _plotdb.erase(j);
                  } else { // j == 2 and i == 3
                     if(skew.at(0) == -16) {
                           skew.at(0) = j->timestamp - i->timestamp;
                     }
                     _plotdb.erase(j);;
                  }
               }
            }
         }
      }
      //fill in skew value between elected Node and other nodes if not found yet
      if(skew.at(0) == -16)  {
         skew.at(0) = skew.at(1) - skew.at(2);
      }
      if(skew.at(1) == -16) {
         skew.at(1) = skew.at(2) + skew.at(0);
      }
      //adjusts time for any entries not made by elected node given the found skews
      for(auto i = _plotdb.begin(); i != _plotdb.end(); i++){
         if(!i->isFlagSet(DBFLAG_UNSKEW)) {
            i->timestamp -= skew.at(i->node_id);
         }
      }
   
      usleep(1000);
   }   
}

/**********************************************************************************************
 * queueNewPlots - looks at the database and grabs the new plots, marshalling them and
 *                 sending them to the queue manager
 *
 *    Returns: number of new plots sent to the QueueMgr
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

unsigned int ReplServer::queueNewPlots() {
   std::vector<uint8_t> marshall_data;
   unsigned int count = 0;

   if (_verbosity >= 3)
      std::cout << "Replicating plots.\n";

   // Loop through the drone plots, looking for new ones
   std::list<DronePlot>::iterator dpit = _plotdb.begin();
   for ( ; dpit != _plotdb.end(); dpit++) {

      // If this is a new one, marshall it and clear the flag
      if (dpit->isFlagSet(DBFLAG_NEW)) {
         
         dpit->serialize(marshall_data);
         dpit->clrFlags(DBFLAG_NEW);

         count++;
      }
      if (marshall_data.size() % DronePlot::getDataSize() != 0)
         throw std::runtime_error("Issue with marshalling!");

   }
  
   if (count == 0) {
      if (_verbosity >= 3)
         std::cout << "No new plots found to replicate.\n";

      return 0;
   }
 
   // Add the count onto the front
   if (_verbosity >= 3)
      std::cout << "Adding in count: " << count << "\n";

   uint8_t *ctptr_begin = (uint8_t *) &count;
   marshall_data.insert(marshall_data.begin(), ctptr_begin, ctptr_begin+sizeof(unsigned int));

   // Send to the queue manager
   if (marshall_data.size() > 0) {
      _queue.sendToAll(marshall_data);
   }

   if (_verbosity >= 2) 
      std::cout << "Queued up " << count << " plots to be replicated.\n";

   return count;
}

/**********************************************************************************************
 * addReplDronePlots - Adds drone plots to the database from data that was replicated in. 
 *                     Deconflicts issues between plot points.
 * 
 * Params:  data - should start with the number of data points in a 32 bit unsigned integer, 
 *                 then a series of drone plot points
 *
 **********************************************************************************************/

void ReplServer::addReplDronePlots(std::vector<uint8_t> &data) {
   if (data.size() < 4) {
      throw std::runtime_error("Not enough data passed into addReplDronePlots");
   }

   if ((data.size() - 4) % DronePlot::getDataSize() != 0) {
      throw std::runtime_error("Data passed into addReplDronePlots was not the right multiple of DronePlot size");
   }

   // Get the number of plot points
   unsigned int *numptr = (unsigned int *) data.data();
   unsigned int count = *numptr;

   // Store sub-vectors for efficiency
   std::vector<uint8_t> plot;
   auto dptr = data.begin() + sizeof(unsigned int);

   for (unsigned int i=0; i<count; i++) {
      plot.clear();
      plot.assign(dptr, dptr + DronePlot::getDataSize());
      addSingleDronePlot(plot);
      dptr += DronePlot::getDataSize();      
   }
   if (_verbosity >= 2)
      std::cout << "Replicated in " << count << " plots\n";   
}


/**********************************************************************************************
 * addSingleDronePlot - Takes in binary serialized drone data and adds it to the database. 
 *
 **********************************************************************************************/

void ReplServer::addSingleDronePlot(std::vector<uint8_t> &data) {
   DronePlot tmp_plot;

   tmp_plot.deserialize(data);

   _plotdb.addPlot(tmp_plot.drone_id, tmp_plot.node_id, tmp_plot.timestamp, tmp_plot.latitude,
                                                         tmp_plot.longitude);
}


void ReplServer::shutdown() {
   _shutdown = true;
}
