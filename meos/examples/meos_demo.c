/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2023, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2023, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. 
 *
 *****************************************************************************/

/**
 * @brief A program that uses the MEOS library for a demonstration of
 * the performance of the library.
 * We include a benchmark of the library with analytics functions.
 * The program can be build as follows
 * @code
 * gcc -Wall -g -I/usr/local/include  -o meos_benchmark meos_benchmark.c -L/usr/local/lib -lmeos 
 * @endcode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <float.h>

/* MEOS */
#include <meos.h>
#include <meos_internal.h>
#include <time.h>

/* Number of instants to send in batch to the file  */
#define NO_INSTANTS_BATCH 500
/* Number of instants to keep when restarting a sequence */
#define NO_INSTANTS_KEEP 2
/* Maximum length in characters of a header record in the input CSV file */
#define MAX_LENGTH_HEADER 1024
/* Maximum length in characters of a point in the input data */
#define MAX_LENGTH_POINT 64
/* Maximum length for a SQL output query */
#define MAX_LENGTH_SQL 16384
/* Number of inserts that are sent in bulk */
#define NO_BULK_INSERT 20
/* Maximum number of trips */
#define MAX_TRIPS 10000
/* Maximum number of cars */
#define MAX_CARS 3385
/* Maximum number of places of interest */
#define MAX_AREAS 3


int count=0;

typedef struct
{
  GSERIALIZED *geom;
} area_record;


typedef struct
{
  Timestamp T;
  long int id;
  long int trj_id;
  double Latitude;
  double Longitude;
  double speed;
  double bearing;
  double accuracy;
} car_point_record;


typedef struct
{
  long int trj_id;   /* Identifier of the trip */
  TSequence *trip; /* Array of Latest observations of the trip, by id */
} trip_record;


int
main(int argc, char **argv)
{
  car_point_record rec;
  area_record areaRead;
  int no_records = 0;
  int no_nulls = 0;
  int no_areas = 2;
  int no_carsBB = 0;
  int no_carsinAreas = 0;
  int no_trips = 0;
  int no_trips2 = 0;
  const Interval *maxt = pg_interval_in("1 day", -1);
  char point_buffer[MAX_LENGTH_POINT];
  char text_buffer[MAX_LENGTH_HEADER];
  /* Allocate space to build the allcars */
  trip_record allcars[MAX_TRIPS] = {0};
  trip_record cars[MAX_CARS] = {0};
  trip_record carsinAreas[MAX_CARS] = {0};
  trip_record carsTrips[MAX_CARS] = {0};
  trip_record carsTrips2[MAX_CARS] = {0};
  /* Allocate space to build the areas */
  area_record areas[MAX_AREAS] = {0};
  /* Number of cars */
  int numcars = 0;
  /* Iterator variable */
  int i;
  /* Return value */
  int return_value = 0;

  /***************************************************************************
   * Section 1: Initialize MEOS, open the input AIS file
   ***************************************************************************/

  /* Initialize MEOS */
  meos_initialize("UTC", NULL);

  /* Get start time */
  clock_t t = clock();
  clock_t t1 = clock();

  /* You may substitute the full file path in the first argument of fopen */
  FILE *fileIn = fopen("data/singaporeumMilhao.csv", "r");

  printf("file in opened\n");

  if (! fileIn)
  {
    printf("Error opening input file\n");
    return_value = 1;
    goto cleanup;
  }

  /***************************************************************************
   * Section 2: Read input file line by line and append each observation as a
   * temporal point in MEOS
   ***************************************************************************/

  //printf("Accumulating %d instants before sending them to the logfile\n" "(one marker every logfile update)\n", NO_INSTANTS_BATCH);
  /* Read the first line of the file with the headers */
  fscanf(fileIn, "%1023s\n", text_buffer);

  /* Continue reading the file */
  do
  {
    int read = fscanf(fileIn, "%32[^,],%ld,%ld,%lf,%lf,%lf,%lf,%lf\n",
      text_buffer, &rec.id,&rec.trj_id, &rec.Latitude, &rec.Longitude, &rec.speed,&rec.bearing,&rec.accuracy);
    /* Transform the string representing the timestamp into a timestamp value */
    rec.T = pg_timestamp_in(text_buffer, -1);

    if (read == 8)
      no_records++;

    if (read != 8 && ! feof(fileIn))
    {
      printf("Record with missing values ignored\n");
      no_nulls++;
    }

    if (ferror(fileIn))
    {
      printf("Error reading file\n");
      fclose(fileIn);
      //fclose(fileOut);
    }

    /* Find the place to store the new instant */
    int car = -1;
    for (i = 0; i < MAX_TRIPS; i++)
    {
      if (allcars[i].trj_id == rec.trj_id)
      {
        car = i;
        break;
      }
    }
    if (car < 0)
    {
      car = numcars++;
      if (car == MAX_TRIPS)
      {
        printf("The maximum number of cars in the input file is bigger than %d",MAX_TRIPS);
        return_value = 1;
        goto cleanup;
      }
      allcars[car].trj_id = rec.trj_id;
    }
    /*
     * Append the latest observation to the corresponding car.
     * In the input file it is assumed that
     * - The coordinates are given in the WGS84 geographic coordinate system
     * - The timestamps are given in GMT time zone
     */
    char *t_out = pg_timestamp_out(rec.T);
    sprintf(point_buffer, "SRID=4326;Point(%lf %lf)@%s+00", rec.Longitude,
        rec.Latitude, t_out);

    //windowManager(NO_INSTANTS_BATCH,cars, car,fileOut);
    free(t_out);
    /* Append the last observation */
    TInstant *inst = (TInstant *) tgeompoint_in(point_buffer);
    if (! allcars[car].trip)
      allcars[car].trip = tsequence_make_exp((const TInstant **) &inst, 1,
        NO_INSTANTS_BATCH, true, true, LINEAR, false);
    else
      tsequence_append_tinstant(allcars[car].trip , inst, 1000, maxt,true);
  } while (!feof(fileIn));

  t1 = clock() - t1;
  double time_taken1 = ((double) t1) / CLOCKS_PER_SEC;
  printf("The program took %f seconds to read and create trajectories\n", time_taken1);

  printf("%d records read.\n%d incomplete records ignored.\n%d cars\n",
    no_records, no_nulls,numcars);


    /***************************************************************************
   * Section 3: Create areas geometry
   ***************************************************************************/
    clock_t t2 = clock();

    char *polygon_wkt_Bay = "SRID=4326;Polygon((103.859 1.2754,103.859 1.2856,103.8705 1.2856,103.8705 1.2754,103.859 1.2754))";
    areas[0].geom = pgis_geometry_in(polygon_wkt_Bay,-1);
    printf("Created Bay\n");

    
    char *polygon_wkt_Airport = "SRID=4326;Polygon((103.9719 1.3213,103.9719 1.3459,103.9904 1.3459,103.9904 1.3213,103.9719 1.3213))";
    areas[1].geom = pgis_geometry_in(polygon_wkt_Airport,-1);
    printf("Created Airport\n");

   /***************************************************************************
   * Section 4: Create bounding box that incorates both areas
   ****************************************************************************/
    /*Creating Bounding Box*/
    char *polygon_wkt_BoundingBox = "SRID=4326;Polygon((103.859 1.2754,103.859 1.3459,
    103.9904 1.3459,103.9904 1.2754,103.859 1.2754))";
    areas[2].geom = pgis_geometry_in(polygon_wkt_BoundingBox,-1);

    /* separate allcars that are near the bounding box */
    for (size_t i = 0; i < numcars; i++)
    {
     if (eintersects_tpoint_geo((const Temporal *) allcars[i].trip, areas[2].geom))
      {
        cars[no_carsBB].trj_id = allcars[i].trj_id;
        cars[no_carsBB].trip = tsequence_copy(allcars[i].trip);
        no_carsBB++;
      }
    }
    printf("%d cars in the bounding box\n", no_carsBB);
  
    t2 = clock() - t2;
    double time_taken2 = ((double) t2) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to create areas and check intersections with the bounding box\n", time_taken2);

  /***************************************************************************
   * Separate carsinAreas from all cars inside the bounding box
   ****************************************************************************/
   clock_t t3 = clock();
    /* separate allcars that are near the areas */
    for (size_t i = 0; i < no_carsBB; i++)
    {
      if (eintersects_tpoint_geo((const Temporal *) cars[i].trip, areas[1].geom)
      || eintersects_tpoint_geo((const Temporal *) cars[i].trip, areas[0].geom))
      {    
        carsinAreas[no_carsinAreas].trj_id = cars[i].trj_id;
        carsinAreas[no_carsinAreas].trip = tsequence_copy(cars[i].trip);

        // char *seq1_wkt = tpoint_as_ewkt((Temporal *) cars[i].trip, 2);
        // char *seq2_wkt = tpoint_as_ewkt((Temporal *) carsinAreas[no_carsinAreas].trip , 2);
        // printf("\nseql: %s\nseq2: %s\n", seq1_wkt, seq2_wkt);
        no_carsinAreas++;
      }
    } 
    printf("%d cars in the areas\n", no_carsinAreas);
    t3 = clock() - t3;
    double time_taken3 = ((double) t3) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to separate carsinAreas from all cars inside the bounding box\n", time_taken3);


   /***************************************************************************
   * Split carsinAreas trips into trips between areas - AtGeometry
   ****************************************************************************/
    clock_t t4 = clock();
    /* Separate trips that are near the areas atSTbox */
    for (int i = 0; i < no_carsinAreas; i++)
    {
      for (int j = 0; j < no_areas+1; j++)
      {
        if (eintersects_tpoint_geo((const Temporal *) carsinAreas[i].trip, areas[j].geom))
        {
          Temporal *atgeom = tpoint_at_geom_time((const Temporal *)carsinAreas[i].trip, areas[j].geom, NULL, NULL);
          //printf("car %ld is in area %d\n", carsinAreas[i].trj_id, j);

          if (atgeom)
          {
            carsTrips[no_trips].trj_id = carsinAreas[i].trj_id;
            carsTrips[no_trips].trip = tsequence_copy(carsinAreas[i].trip);
            no_trips++;
            free(atgeom);
          }
        }
      }
    }
    printf("%d trips between areas with atGeometry\n", no_trips);
    t4 = clock() - t4;
    double time_taken4 = ((double) t4) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to separate trips between areas with atGeometry\n", time_taken4);


   /***************************************************************************
   * Split carsinAreas trips into trips between areas - AtStops
   ****************************************************************************/
     /* Separate trips that are near the areas atSTbox */
    // for (int i = 0; i < no_carsinAreas; i++)
    // {
    //   for (int j = 0; j < no_areas; j++)
    //   {
    //     char *seq_wkt = tpoint_as_ewkt((Temporal *) carsinAreas[i].trip, 2);
    //     printf("\nseql: %sn", seq_wkt);
    //     TSequenceSet * atstops =  temporal_stops((const Temporal *)carsinAreas[i].trip,25,'one day');
    //     if (1)
    //     {
    //       printf("\n car %ld is in area %d\n", carsinAreas[i].trj_id, j);
    //       carsTrips2[no_trips2].trj_id = carsinAreas[i].trj_id;
    //       carsTrips2[no_trips2].trip = tsequence_copy(carsinAreas[i].trip);
    //       char *seq1_wkt = tpoint_as_ewkt((Temporal *) carsinAreas[i].trip, 2);
    //       char *seq2_wkt = tpoint_as_ewkt((Temporal *) carsTrips2[no_trips2].trip , 2);
    //       printf("\nseql: %s\nseq2: %s\n", seq1_wkt, seq2_wkt);

    //       no_trips2++;
    //       //free(atstops);
    //     }
    //   }
    // }
    // printf("%d trips between areas with atStops\n", no_trips2);
  

   /***************************************************************************
   * Section 5 : Trips Functions
   ****************************************************************************/

    /* Total distance */
    clock_t t5 = clock();
    double totalDistance = 0;
    double dist;
    TSequence * speed[MAX_CARS] = {0};
    
    for (int i = 0; i < numcars; i++)
    {
      dist = tpoint_length((const Temporal *) allcars[i].trip);
      totalDistance += dist;
    }
    printf("Total Distance is %lf\n", totalDistance);
    t5 = clock() - t5;
    double time_taken5 = ((double) t5) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to calculate distance\n", time_taken5);


    clock_t t6 = clock();
    /* Speed */
    for (int i = 0; i < numcars; i++)
    {
      speed[i] = (TSequence  *)tpointseq_speed((const TSequence *) allcars[i].trip);
      size_t length;
    }
    t6 = clock() - t6;
    printf("Total Distance is %lf\n", totalDistance);
    double time_taken6 = ((double) t6) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to calculate speed\n", time_taken6);



   /***************************************************************************
   * Section 6: Car that get less than 1 km from each other
   ****************************************************************************/
    int nuncars_tooClose = 0;
    int ids[MAX_CARS] = {0};
    clock_t t7 = clock();

    /* Find cars that are near carsinAreas within 1 meter */
    for (size_t i = 0; i < numcars; i++)
    {
      for (size_t j = 0; j < i+1; j++)
      {
        if (allcars[i].trj_id != allcars[j].trj_id)
        {
          Temporal * aux = tdwithin_tpoint_tpoint((const Temporal *)allcars[i].trip, (const Temporal *)allcars[j].trip,1000000,false,false);
          if (aux)
          {
            if (ids[i] == 0 && ids[j] == 0)
            {
              ids[i]= 1;
              ids[j]= 1;
              nuncars_tooClose++;
            }
            free(aux);
          }
        }
      }
    }
    printf("%d cars that get less than 1 km from each other\n", nuncars_tooClose);
    t7 = clock() - t7;
    double time_taken7 = ((double) t7) / CLOCKS_PER_SEC;
    printf("The program took %f seconds to calculate cars that get less than 1 km from each other\n", time_taken7);

    /***************************************************************************/

  t = clock() - t;
  double time_taken = ((double) t) / CLOCKS_PER_SEC;
  printf("The program took %f seconds to execute\n", time_taken);

/* Clean up */
cleanup:
 /* Free memory */
  for (i = 0; i < numcars; i++)
    free(allcars[i].trip);

  /* Finalize MEOS */
  meos_finalize();

  // /* Close the connection to the logfile */
  fclose(fileIn);

  return return_value;
}
