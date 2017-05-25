#include <Common.h>
#include <IO.h>

#include <SPI.h>
#include <SD.h>
#include <ElcanoSerial.h>

using namespace elcano;
SerialData data;
ParseState ps; 

#define PI ((float) 3.1415925)
#ifndef NULL
#define NULL 0 
#endif
#define currentlocation  -1 
#define EMPTY  -2

//void DataReady();
extern bool DataAvailable;

int map_points = 23;
/*---------------------------------------------------------------------------------------*/ 
// EDIT for route
// CONES includes start and stop
#define CONES 1
//int DESTINATION_INDEX = 1;
//long goal_lat[CONES] = {47760934};
//long goal_lon[CONES] = {-122189963};
//long goal_lat[CONES] = {  47621881,   47621825,   47623144,   47620616,   47621881};
//long goal_lon[CONES] = {-122349894, -122352120, -122351987, -122351087, -122349894};
/*  mph   mm/s
     3    1341
     5    2235
     8    3576
     10   4470
     15   6706
     20   8941
     25  11176 
     30  13411
     45  20117
     60  26822
*/
#define DESIRED_SPEED_mmPs 2235
/*---------------------------------------------------------------------------------------*/ 

//Maximum

#define MAX_MAPS 10         // The maximum number of map files stored to SD card.
#define MAX_WAYPOINTS 40    // The maximum number of waypoints in each map file.

struct junction Nodes[MAX_WAYPOINTS];

struct AStar
{
    int ParentID;
    long CostFromStart;
    long CostToGoal;
    long TotalCost;
}Open[MAX_WAYPOINTS];


class waypoint Origin, Start;
waypoint Path[MAX_WAYPOINTS];  // course route to goal

waypoint mission[CONES];  // aka MDF //The target Nodes to hit
int waypoints = CONES;

/*---------------------------------------------------------------------------------------*/ 
// Fill the distances of the junctions in the MAP
void ConstructNetwork(junction *Map, int MapPoints)
{
  double deltaX, deltaY;
  int destination;
  for (int i = 0; i < MapPoints; i++)
  {
      if( Map[i].east_mm == INVALID)  continue;
      for (int j = 0;  j< 4; j++)
      {
        destination = Map[i].destination[j];
        if (destination == END) continue;
        deltaX = Map[i].east_mm;
        deltaX -= Map[destination].east_mm;
        deltaY = Map[i].north_mm;
        deltaY -= Map[destination].north_mm;
       
        Map[i].Distance[j] *= sqrt(deltaX * deltaX + deltaY * deltaY); //in rough scale
      }
  }
}

/*---------------------------------------------------------------------------------------*/
// Find the distance from (east_mm, north_mm) to a road segment Nodes[i].distance[j]
// return distance in mm, and per cent of completion from i to j.
// distance is negative if (east_mm, north_mm) lies to the left of the road
// when road direction is from i to j

// Compare this routine to distance() in C3 Pilot
//k =  index into Nodes[]
//east_mm : current
long distance(int cur_node, int *k,  long cur_east_mm, long cur_north_mm, int* perCent)  
{
  float deltaX, deltaY, dist_mm;
  int cur , destination;
  long Eunit_x1000, Nunit_x1000;
  long closest_mm = MAX_DISTANCE; 
  long Road_distance, RoadDX_mm, RoadDY_mm;

  long pc; //per cent of completion from i to j.
  
  *perCent = 0;
  *k = 0;
  closest_mm = MAX_DISTANCE;

 
  for (cur = 0; cur < 4; cur++)  // Don't make computations twice.
  {   
      destination = Nodes[cur_node].destination[cur];
      if (destination == 0 || destination < cur_node) continue;  //replace Destination with END
      // compute road unit vectors from i to cur
      RoadDX_mm = Nodes[destination].east_mm - Nodes[cur_node].east_mm;
      int Nunit_x1000 = RoadDY_mm * 1000 / Nodes[cur_node].Distance[cur];
//
      deltaX = cur_east_mm - Nodes[cur_node].east_mm;
      deltaY = cur_north_mm - Nodes[cur_node].north_mm;
//
//      
      Road_distance = sqrt( (RoadDX_mm * RoadDX_mm) + (RoadDY_mm * RoadDY_mm));
      pc = (deltaX * Eunit_x1000 + deltaY * Nunit_x1000) / (Nodes[cur_node].Distance[cur] * 10);
  
      if (abs(Road_distance) < abs(closest_mm) && pc >= 0 && pc <= 100)
      {
          closest_mm = Road_distance;
          *k = destination;
          *perCent = pc;
          
      }
  }
  return closest_mm;
}
/*---------------------------------------------------------------------------------------*/

void FindClosestRoad(waypoint *start, waypoint *road)  //populate road with best road from start 
{
  long closest_mm = MAX_DISTANCE;
  long dist;
  int close_index;
  int perCent;
  long done = 1;//2000;
  int i, node_successor;
  
  for (i = 0; i < map_points; i++)  // find closest road.
  {
    dist = distance(i, &node_successor, start->east_mm, start->north_mm, &perCent); //next node to visit 

      if (abs(dist) < abs(closest_mm))
      {
        close_index = node_successor;
        closest_mm = dist;
        done = perCent; //Not really true amount of nodes done?
        road->index = i;
        road->sigma_mm = node_successor;
      }
  }
  if (closest_mm < MAX_DISTANCE)
  {
    i = road->index; //0
    node_successor = close_index; //0
    road->east_mm =  Nodes[i].east_mm  +  done *(Nodes[node_successor].east_mm  - Nodes[i].east_mm) / 100;
    road->north_mm = Nodes[i].north_mm + done *(Nodes[node_successor].north_mm - Nodes[i].north_mm) / 100;
 }
 else
 {
   for (i = 0; i < map_points; i++) // find closest node
   {
      dist = start->distance_mm(Nodes[i].east_mm, Nodes[i].north_mm);
      
      if (dist < closest_mm)
      {
          close_index = i;
          closest_mm = dist;
      }
   }
    road->index = road->sigma_mm = close_index;
    road->east_mm =  Nodes[close_index].east_mm;
    road->north_mm = Nodes[close_index].north_mm;
   }
  
  road->Evector_x1000 = 1000;
  road->Nvector_x1000 = 0;
  road->time_ms = 0;
  road->speed_mmPs = DESIRED_SPEED_mmPs;

//  Test FindClosest Road:
//  Serial.println("Distance " + String(dist));
//  Serial.println("Road :  East_mm " + String(road->east_mm) + "\t North_mm " + String(road->north_mm));
  
}

//Test ClosestRoad:
void test_closestRoad(){
  
  waypoint roadOrigin;
  waypoint roadDestination;
  
  Serial.println("First " );
  FindClosestRoad(&mission[0], &roadOrigin);

  Serial.println();
  
  Serial.println("Second ");
  FindClosestRoad(&mission[3], &roadDestination);
  for(int last = 0; last < 4; last++){
//    Serial.println(" mission " + String(mission[last].east_mm ) + "\t roadOrigin " + String(roadOrigin.east_mm));   
//    Serial.println(" mission " + String(mission[last].longitude ) + "\t roadOrigin " + String(roadOrigin.east_mm));  
  }
  for(int last = 0; last < 4; last++){
    Serial.println(" mission " + String(mission[last].east_mm ) + "\t roadOrigin " + String(roadDestination.east_mm));   
    Serial.println(" mission " + String(mission[last].longitude ) + "\t roadOrigin " + String(roadDestination.north_mm));  
  }
}
/*---------------------------------------------------------------------------------------*/
// start and destination are on the road network given in Nodes.
// start is in Path[1].
// Place other junction waypoints into Path.
// Returned value is next index into Path.
// start->index identifies the closest node.
// sigma_mm holds the index to the other node.
// A* is traditionally done with pushing and popping node from an Open and Closed list.
// Since we have a small number of nodes, we instead reserve a slot on Open and Closed
// for each node. 

int BuildPath (int j, waypoint* start, waypoint* destination)// Construct path backward to start.
{
  int last = -1;
  int route[map_points];
  int i, k, node;
  long dist_mm;

   k = map_points - 1;
   route[k] = j;
   Open[0].ParentID = -1;
   
   while(Open[j].ParentID != currentlocation)
   {
      j = route[--k] = Open[j].ParentID;
   }

  
   Path[last] = start;
   
   for (; k < map_points; k++)
   {
     node = route[k];
     last++;

     Path[last].east_mm = Nodes[node].east_mm;
     Path[last].north_mm  = Nodes[node].north_mm;
      
  }
  for (k = 0; k <= last; k++)
  {
     if(k > 0) Path[k].sigma_mm = 10;  // map should be good to a cm.
     Path[k].index = k;
     Path[k].speed_mmPs = DESIRED_SPEED_mmPs;
//     Path[k].Compute_LatLon();  // this is never used
  }
  last++;
  for (j = 0; j < last-1; j++)
  {
      Path[j].vectors(&Path[j+1]);
  }

  return last;
} 
/*---------------------------------------------------------------------------------------*/

void test_buildPath(){
  
//  BuildPath(0, Path, destination);
}
int FindPath(waypoint *start, waypoint *destination)//While OpenSet is not empty
{

//  Serial.println("StartIndex " + String(start->east_mm)+ "\t DestinationINdex " + String(destination->east_mm));
//  Serial.println("StartIndex " + String(start->north_mm)+ "\t DestinationINdex " + String(destination->north_mm));

  long ClosedCost[map_points];
  int  i, neighbor, k;
  long NewCost, NewStartCost, NewCostToGoal;
  long NewIndex;
  long BestCost, BestID;
  bool Processed = false;

  for (i = 0; i < map_points; i++)// mark all nodes as empty
  { 
    Open[i].TotalCost = MAX_DISTANCE;
    ClosedCost[i] = MAX_DISTANCE;
  }
  
   i = start->index; // get successor nodes of start
   Open[i].CostFromStart = start->distance_mm(Nodes[i].east_mm, Nodes[i].north_mm); 
   Open[i].CostToGoal = destination->distance_mm(Nodes[i].east_mm, Nodes[i].north_mm);
   Open[i].TotalCost = Open[i].CostFromStart + Open[i].CostToGoal;

   Open[i].ParentID = currentlocation;

   i = start->sigma_mm;
   Open[i].CostFromStart = start->distance_mm(Nodes[i].east_mm, Nodes[i].north_mm);
   Open[i].CostToGoal = destination->distance_mm(Nodes[i].east_mm, Nodes[i].north_mm);
   Open[i].TotalCost = Open[i].CostFromStart + Open[i].CostToGoal;
   Open[i].ParentID = currentlocation;   

   while (BestCost < MAX_DISTANCE) //While OpenSet is not empty
   { 
      BestCost = MAX_DISTANCE;
      BestID = -1;
     // pop lowest cost node from Open; i.e. find index of lowest cost item
       for (i = 0; i < map_points; i++)
       {
          if (Open[i].TotalCost < BestCost )
          {
            BestID = i;
            BestCost = Open[i].TotalCost;
          }
          
       }
       if (BestID < 0)
       {
         return INVALID;
       }
       Serial.println("BestID " + String(BestID) + " BestCost " + String(BestCost));

        Open[BestID].TotalCost = MAX_DISTANCE;  // Remove node from "stack".  
       if (BestID == destination->index || BestID == destination->sigma_mm)// Done:: reached the goal!!
       {  
          return BuildPath(BestID, start, destination);   // Construct path backward to start. 
       }

      i = BestID;  // get successor nodes from map
      
      for (neighbor = 0; neighbor < map_points; neighbor++)
      {
          NewIndex = Nodes[i].destination[neighbor];  

          if (NewIndex == END)continue; // No success in this slot
              
          NewStartCost =  Open[i].CostFromStart + Nodes[i].Distance[neighbor]; 
          NewCostToGoal = destination->distance_mm(Nodes[NewIndex].east_mm, Nodes[NewIndex].north_mm);
          NewCost = NewStartCost + NewCostToGoal;
          
          
          if (NewCost >= ClosedCost[NewIndex]) // check if this node is already on Open or Closed.
              continue;  // Have already looked at this node
              
          else if (ClosedCost[NewIndex] != MAX_DISTANCE) // looked at this node before, but at a higher cost
          { 
              ClosedCost[NewIndex] = MAX_DISTANCE;  // remove node from Closed
          }
          if (NewCost >= Open[NewIndex].TotalCost)
              continue;   // This node is a less efficient way of getting to a node on the list
          // Push successor node onto stack.

          Open[NewIndex].CostFromStart = NewStartCost;
          Open[NewIndex].CostToGoal = NewCostToGoal;
          Open[NewIndex].TotalCost = NewCost;
          Open[NewIndex].ParentID = i;
      }  // end of successor nodes

    ClosedCost[BestID] =  BestCost; // Push node onto Closed
  }  

  
  return 0;  // failure
}

/*---------------------------------------------------------------------------------------*/
// Low level path is a straight line from start to detination.
// PathPlan makes an intermediate level path that uses as many roads as possible.
//start = currentlocation: destination = heading to;
int PlanPath (waypoint *start, waypoint *destination)
 {

     Serial.println("Start : East_mm = " + String(start->east_mm) + "\t North_mm =  " + String(start->north_mm));
     waypoint roadOrigin, roadDestination;
     
     int last = 0;
     Path[0] = start;
     Path[0].index = 0;
     
     FindClosestRoad( start, &roadOrigin );
     FindClosestRoad( destination, &roadDestination ); 

     Serial.println("Start_East_mm " + String(start->east_mm));
     Serial.println("Road_Origin_east_mm " + String(roadOrigin.east_mm));
     int w = abs(start->east_mm  - roadOrigin.east_mm) + abs(start->north_mm - roadOrigin.north_mm);
     int x = abs(destination->east_mm  - roadDestination.east_mm)  + abs(destination->north_mm - roadDestination.north_mm);
     int straight_dist = 190 * abs(start->east_mm  - destination->east_mm)+  abs(start->north_mm - destination->north_mm);
    if (w + x >= straight_dist) // don't use roads; go direct
     { 
        last = 1;
        Serial.println("In Straight");
     }
      
      else   // use A* with the road network
      {
        Path[1] = roadOrigin;
        Path[1].index = 1;
        last = FindPath(&roadOrigin, &roadDestination);
      }
      
      Path[last] = destination;
      Path[last-1].vectors(&Path[last]);
      Path[last].Evector_x1000 = Path[last-1].Evector_x1000;
      Path[last].Nvector_x1000 = Path[last-1].Nvector_x1000;
      Path[last].index = last | END;

    return last;
   
}
/*---------------------------------------------------------------------------------------*/

// Transmit the path to C3 Pilot over a serial line.
void SendPath(waypoint *course, int count)
{
  
  SerialData results;
  Serial.println();
  for( int i = 0; i < count; i++)
  {
//     results.clear();
//     results.number = i;
//     results.kind = MsgType::seg;
//     results.posE_cm = course[i].east_mm;
//     results.posN_cm = course[i].north_mm;
//     float angle = atan2(course->Nvector_x1000, course->Evector_x1000) * 180 / PI + 90.;//data.posE_cm;
//     results.bearing_deg = (long) (-angle); //data.bearing_deg + 8; // (long) (-angle);
//     results.speed_cmPs = course->speed_mmPs / 10;//data.speed_cmPs + 9; 
//     results.write(&Serial2);  
//     delay(100);

       Serial.print(String(i) + " i   ");
       Serial.print(course[i].east_mm);
       Serial.print("\t");
       Serial.println(course[i].north_mm);
  }

    Serial.println();

}

//Converts provided Longitude and Latitude to MM
void convertLatLonToMM(long *latitude, long *longitude){
  long diff = *longitude - LONGITUDE_ORIGIN;
  double relative = ((double)diff)/1000000. * (PI /180.) * cos(((double) LATITUDE_ORIGIN/1000000. * (PI/180.)));
  relative *= EARTH_RADIUS_MM;
  *longitude = relative;

  diff = *latitude - LATITUDE_ORIGIN;
  *latitude = ((double)diff)/1000000. *  (PI /180.) * EARTH_RADIUS_MM;
}

/*---------------------------------------------------------------------------------------*/
// LoadMap
// Loads the map nodes from a file.
// Takes in the name of the file to load and loads the appropriate map. 
// Returns true if the map was loaded.
// Returns false if the load failed.
boolean LoadMap(char* fileName)
{
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File myFile = SD.open(fileName, FILE_READ);
  
  // if the file opened okay, read from it:
  if (myFile) {
    // Initialize a string buffer to read lines from the file into
    // Allocate an extra char at the end to add null terminator
    char* buffer = (char*)malloc(myFile.size()+1);  

    // index for the next character to read into buffer
    char* ptr = buffer;
    
    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      *ptr = myFile.read();
      ++ptr;
    }

    // Null terminate the buffer string
    *ptr = '\0';
    
    // Set up tokenizer for the file buffer string
    char delimiter[2] = ",\n";
    char* token;
    int col = 0;
    int row = 0;
    
    // get the first token 
    token = strtok(buffer, delimiter);

    // walk through other tokens
    while( token != NULL ) 
    {
      switch (col % 10)
      {
        case 0:  // latitude
          Nodes[row].east_mm = atol(token);
          col++;
        break;
        
        case 1:  // longitude
          Nodes[row].north_mm = atol(token);
          col++;
        break;
        
        case 2:  // filename
          if (token == "END")
          {
            Nodes[row].destination[0] = END;
          }
          else
          {
            Nodes[row].destination[0] = atoi(token);
          }
          col++;
        break;
        
        case 3:  // filename
          if (token == "END")
          {
            Nodes[row].destination[1] = END;
          }
          else
          {
            Nodes[row].destination[1] = atoi(token);
          }
          col++;
        break;

        case 4:  // filename
          if (token == "END")
          {
            Nodes[row].destination[2] = END;
          }
          else
          {
            Nodes[row].destination[2] = atoi(token);
          }
          col++;
        break;

        case 5:  // filename
          if (token == "END")
          {
            Nodes[row].destination[3] = END;
          }
          else
          {
            Nodes[row].destination[3] = atoi(token);
          }
          col++;
        break;

        case 6:  // filename
          Nodes[row].Distance[0] = atol(token);
          col++;
        break;

        case 7:  // filename
          Nodes[row].Distance[1] = atol(token);
          col++;
        break;

        case 8:  // filename
          Nodes[row].Distance[2] = atol(token);
          col++;
        break;

        case 9:  // filename
          Nodes[row].Distance[3] = atol(token);
          convertLatLonToMM(&Nodes[row].east_mm, &Nodes[row].north_mm);
          col++;
          row++;
        break;

        default:  // unexpected condition; print error
          Serial.print("Unexpected error happened while reading map description file.");
          Serial.print("Please verify the file is in the correct format.");
          Serial.println("Planner may not work correctly if this message appears.");
          break;
      }
      token = strtok(NULL, delimiter);
    }
    map_points = row;

//    //To test LoadMap:
//    for (int i = 0; i < map_points; i++)
//    {
//      Serial.print(Nodes[i].east_mm);
//      Serial.print(",");
//      Serial.print(Nodes[i].north_mm);
//      Serial.print(",");
//      Serial.print(Nodes[i].destination[0]);
//      Serial.print(",");
//      Serial.print(Nodes[i].destination[1]);
//      Serial.print(",");
//      Serial.print(Nodes[i].destination[2]);
//      Serial.print(",");
//      Serial.print(Nodes[i].destination[3]);
//      Serial.print(",");
//      Serial.print(Nodes[i].Distance[0]);
//      Serial.print(",");
//      Serial.print(Nodes[i].Distance[1]);
//      Serial.print(",");
//      Serial.print(Nodes[i].Distance[2]);
//      Serial.print(",");
//      Serial.println(Nodes[i].Distance[3]);
//    }
    
    // If file loaded, read data into Nodes[]
    free(buffer);
    myFile.close();
  } 
  else {
    
    // if the file didn't open, print an error:
    myFile.close();
    Serial.print("error opening ");
    Serial.print(fileName);
    return false;
  }  
  return true;
}

int DESTINATION_INDEX = 1;
long goal_lat[CONES] = {47760934};
long goal_lon[CONES] = {-122189963};


/*---------------------------------------------------------------------------------------*/ 
// Set up mission structure from cone latitude and longitude list.
void GetGoals(junction *nodes , int Goals)
{
  double deltaX, deltaY, Distance;
  for (int i = 0; i < CONES; i++)
  {
    mission[i].latitude  = goal_lat[i];
    mission[i].longitude = goal_lon[i];
//    //mission[i].
    mission[i].Compute_mm();  
//    Serial.println("mission = " + String(Nodes[DESTINATION_INDEX].east_mm));
    mission[i].speed_mmPs = DESIRED_SPEED_mmPs;
    mission[i].index = 1 | GOAL;
    mission[i].sigma_mm = 1000;
    mission[i].time_ms = 0;
    
    if (i == 0) //If CONE == 1
    {
      mission[i].Evector_x1000 = 1000;
      mission[i].Nvector_x1000 = 0;
    }
    else
    {
      deltaX = mission[i].east_mm - mission[i-1].east_mm;
      deltaY = mission[i].north_mm - mission[i-1].north_mm;
      Distance = sqrt(deltaX*deltaX + deltaY*deltaY);
      mission[i-1].Evector_x1000 = (deltaX * 1000.) / Distance;
      mission[i-1].Nvector_x1000 = (deltaY * 1000.) / Distance;
    }
    if (i == CONES - 1)
    {
      mission[i].Evector_x1000 = mission[i-1].Evector_x1000;
      mission[i].Nvector_x1000 = mission[i-1].Nvector_x1000;
      mission[i].index |= END;
    }
  }

  //Test Mission::
//  for(int i = 0; i < CONES; i++){
//    Serial.println("mission " + String(i) + " east_mm " + 
//    String(mission[i].east_mm) + "\t mission north_mm " + String(mission[i].north_mm));
//    Serial.println("Nodes  east_mm " +  String(Nodes[i].east_mm) + "\t Nodes north_mm " + String(Nodes[i].north_mm));
//  }
}
/*---------------------------------------------------------------------------------------*/
// SelectMap:  Return the file name of the closest origin 
// Determines which map to load.
// Takes in the current location as a waypoint and a string with the name of the file that
//   contains the origins and file names of the maps. 
// Determines which origin is closest to the waypoint and returns it as a junction.
// Assumes the file is in the correct format according to the description above.
void SelectMap(waypoint currentLocation, char* fileName, char* nearestMap)
{
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File myFile = SD.open(fileName, FILE_READ);
  
  // if the file opened okay, read from it:
  if (myFile) {
    
    // Initialize a string buffer to read lines from the file into
    // Allocate an extra char at the end to add null terminator
    char* buffer = (char*)malloc(myFile.size()+1);    
    
    //Serial.println("Printing file info");
    // index for the next character to read into buffer
    char* ptr = buffer;
    
    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      *ptr = myFile.read();
      ++ptr;
    }

    // Null terminate the buffer string
    *ptr = '\0';

    // Set up storage for coordinates and file names
    // Note: we malloc here because the stack is too small
    float* map_latitudes = (float*)malloc(MAX_MAPS * 4);
    float* map_longitudes = (float*)malloc(MAX_MAPS * 4);
    char** map_file_names = (char**)malloc(MAX_MAPS * sizeof(char*)); 

    for(int i = 0; i < MAX_MAPS; i++)
    {
      // initialize using invalid values so that we can ensure valid data in allocated memory
      map_latitudes[i] = 91;  
      map_longitudes[i] = 181;
      map_file_names[i] = "";
    }
    
    // Set up tokenizer for the file buffer string
    char delimiter[] = ", \n";
    char* token;
    int col = 0;
    int row = 1;
    
    // get the first token 
    token = strtok(buffer, delimiter);
    // fill the map_latitude, map_longitude, & map_file with tokens
    while( token != NULL ) 
    {
      switch (col % 3)
      {
        case 0:  // latitude
        map_latitudes[row] = atof(token);
        col++;
        break;
        
        case 1:  // longitude
        map_longitudes[row] = atof(token);
        col++;
        break;
        
        case 2:  // filename
        map_file_names[row] = token;
        col++;
        break;
        
        default:  // unexpected condition; print error
        Serial.println("Unexpected error happened while reading map description file. Please verify the file is in the correct format. Planner may not work correctly if this message appears.");
        break;
      }
      token = strtok(NULL, delimiter);
    }
    
    int closestIndex = -1;
    long closestDistance = MAX_DISTANCE;
    for (int i = 0; i < MAX_MAPS; i++)
    {
      int dist = sqrt((map_latitudes[i] - currentLocation.latitude)*(map_latitudes[i] - currentLocation.latitude) + 
        (map_longitudes[i] - currentLocation.longitude)*(map_longitudes[i] - currentLocation.longitude));
      if (dist < closestDistance) 
      {
        closestIndex = i;
        closestDistance = dist;
      }
    }
    if (closestIndex >= 0)
    {
      // Determine closest map to current location
      // Update Origin global variable
      Origin.latitude = map_latitudes[closestIndex];
      Origin.longitude = map_longitudes[closestIndex];
      for (int i = 0; i < 13; i++)
      {
        nearestMap[i] = map_file_names[closestIndex][i];
      }
      
    }
    else
    {
      Serial.println("error determining closest map.");
    }
    // Free the memory allocated for the buffer    
    free(buffer);
    
    free(map_latitudes);
    free(map_longitudes);
    free(map_file_names);

    // close the file:
    myFile.close();
    Serial.println("Map definitions loaded.");
  } else {
    
    // if the file didn't open, print an error:
    
    myFile.close();
    Serial.println("error opening map_defs.txt");
  }
 
}

/*---------------------------------------------------------------------------------------*/ 
void initialize()
{
  int last; 
  Origin.Evector_x1000 = INVALID; 
  Origin.Nvector_x1000 = INVALID; 
  Origin.east_mm = 0;
  Origin.north_mm = 0;  
  Origin.latitude = INVALID;
  Origin.longitude = INVALID; 

  Start.east_mm = 0;
  Start.north_mm = 0;
  
  Serial.println("Initializing SD card...");
  pinMode(SS, OUTPUT);
   
  if (!SD.begin(chipSelect)) {
    Serial.println("initialization failed!");
  }
  Serial.println("initialization done.");
  char nearestMap[13] = "";

//  SelectMap(Start,"MAPDEFS.TXT",nearestMap); //pollutes info from map_def to nearestMap
  
 LoadMap("MOCKMAP.TXT");// use nearestMap:: pollutes the Node, juction with the nearestMap

  
  ConstructNetwork(Nodes, map_points); //To fill out the rest of the nodes info
  
  //To test LoadMap:
    for (int i = 0; i < map_points; i++)
    {
      Serial.print(Nodes[i].east_mm);
      Serial.print(",");
      Serial.print(Nodes[i].north_mm);
      Serial.print(",");
      Serial.print(Nodes[i].destination[0]);
      Serial.print(",");
      Serial.print(Nodes[i].destination[1]);
      Serial.print(",");
      Serial.print(Nodes[i].destination[2]);
      Serial.print(",");
      Serial.print(Nodes[i].destination[3]);
      Serial.print(",");
      Serial.print(Nodes[i].Distance[0]);
      Serial.print(",");
      Serial.print(Nodes[i].Distance[1]);
      Serial.print(",");
      Serial.print(Nodes[i].Distance[2]);
      Serial.print(",");
      Serial.println(Nodes[i].Distance[3]);
    }
/* Convert latitude and longitude positions to flat earth coordinates.
   Fill in waypoint structure  */
  GetGoals(Nodes, CONES); 
 // SendPath(mission, CONES); //send mission to C3

//  SendPath(mission, CONES); //send mission to C3
   
//  last = PlanPath (&mission[0], &mission[3]);
//  Path[last].index |= GOAL; //number of waypoint sequences
   //SendPath(Path, MAX_WAYPOINTS); //Send a complete Path to C3
   
//   SendPath(Path, MAX_WAYPOINTS); //Send a complete Path to C3
   
   Serial.println();     

}
/*---------------------------------------------------------------------------------------*/ 
void setup() 
{ 
  Serial.begin(9600); 
  Serial.flush();
  //  pinMode(DATA_READY, INPUT);
   DataAvailable = true; //false
  //attachInterrupt(0, DataReady, FALLING);
    
    initialize();
    Serial1.begin(baudrate);
    Serial2.begin(baudrate);
    ps.dt = &data;
    ps.input = &Serial2;
    ps.output = &Serial2;
    ps.capture  =  MsgType::sensor;

}
/*---------------------------------------------------------------------------------------*/ 
//Test mission::a list of waypoints to cover
void test_mission(){

  for(int i = 0; i < map_points; i++)
  {
    Serial.println("mission " + String(i) + " = " +
    String(mission[i].latitude) +"\t north_mm " + String(mission[i].north_mm));
  }
}

void loop() 
{
                              
  static int Goal = 2;
  int last;//what is last?
  /*
    Maintain a list of waypoints from current position to destination.
    Eliminate any waypoints that have already been visited.
    About once a second, write the current position and remaining mission, so that if we
    get a reset, we can start from where we left off.   
  */
//     if (DataAvailable)
//        {
//            ParseStateError r = ps.update();// read vehicle estimated position from C6
//            if(r == ParseStateError::success) 
//            {
              digitalWrite(C4_DATA_SENT, HIGH);  // transition interrupts the processor
              digitalWrite(C4_DATA_SENT, LOW);

              //Start.readPointString(1000, 0);  
//              last = 0;

              DESTINATION_INDEX = 7;

              Start.east_mm = Nodes[0].east_mm;//Path[last].east_mm;
              Start.north_mm = Nodes[0].north_mm;
            
              mission[0].index = DESTINATION_INDEX;
              mission[0].east_mm = Nodes[DESTINATION_INDEX].east_mm;
              mission[0].north_mm = Nodes[DESTINATION_INDEX].north_mm;

            
              last = PlanPath (&Start, &mission[0]);
              Path[last].index |= GOAL;

              // TO DO: Make a fine path, providing proper curve path
//              if (last < MAX_WAYPOINTS / 2 && Goal < CONES)
//              {
//                last = PlanPath (&Start, &mission[++Goal]);
//                Path[last].index |= GOAL;
//              }
              
              for(int i = 0; i < last; i++){
                  Serial.println("i = " + String(i) + "\t East_mm " + String(Path[i].east_mm) + 
                                 "\t North_mm " + String(Path[i].north_mm));
              }
             // SendPath(Path, last); // send data to C3
              
              long end_time = millis() + 1000;// delay for 1 sec, waiting for data
              
              while (millis() < end_time)
              {
                if (DataAvailable) break;
              }
}


///MOCK_MAP.TXTT
void test_path(){
  
  for(int i = 0; i < 4; i++){
    Serial.println("Path " + String(Path[i].east_mm));
  }
  
}
void test_distance()
{
  int percent ;
  int destination;
  int dist = distance(0, &destination ,0 , 0, &percent);
  
  Serial.println("dist " + String(dist));

}

  
