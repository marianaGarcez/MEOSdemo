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
 * the capabilities of the library.
 * Load planes trajectories into the structure 
 * Create regions 
 * *  Range Queries
 * 1- List planes that passes at a region
 * 2- List planes that were within a region from Regions during a period from Periods.
 * 3- List the pairs of planes that were both located within a region from Regions during a period from Periods.
 * 4- List the first time at which a plane visited a point in Points.
 *  * Temporal Aggregate Queries
 * 5- Compute how many planes were active at each period in Periods.
 * 6- Count the number of trips that were active during each hour in Jun 27, 2022.
 * Distance Queries
 * 7- list the distance for each plane
 * 8- List the minimum distance ever between each plane 
 *  * Nearest-Neighbor Query
 * 9- For each trip from Trips, list the three planes that are closest to that plane
 * @code
 * gcc -Wall -g -I/usr/local/include  -o openSky openSky.c -L/usr/local/lib -lmeos
 * @endcode
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <proj.h>
/* MEOS */
#include <meos.h>
#include <meos_internal.h>
#include <time.h>



/* Number of instants to send in batch to the file  */
#define NO_INSTANTS_BATCH 13
/* Number of instants to keep when restarting a sequence */
#define NO_INSTANTS_KEEP 2
/* Maximum length in characters of a header record in the input CSV file */
#define MAX_LENGTH_HEADER 74
/* Maximum length in characters of a point in the input data */
#define MAX_LENGTH_POINT 64
/* Number of inserts that are sent in bulk */
#define NO_BULK_INSERT 20
/* Maximum number of trips */
#define MAX_TRIPS 200001
/* Maximum number of planes */
#define MAX_PLANES 3963



typedef struct
{
  GSERIALIZED *geom;
} area_record;


typedef struct
{
		Timestamp T;
    long int trj_id;
    float lat;
    float lon;
    float velocity;
    float heading;
    float vertrate;
    float baroaltitude;
    float geoaltitude;
} plane_point_record;

typedef struct
{
  long int trj_id; /* Identifier of the trip */
  TSequence *trip; /* Array of Latest observations of the trip, by id */
} trip_record;

void query1(int num_planes, trip_record planes[MAX_TRIPS], area_record continents[7]){
  for (int i = 0; i < num_planes; i++)
  {
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[0].geom))
      printf("Plane %ld passes at Africa\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[1].geom))
      printf("Plane %ld passes at Antarctica\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[2].geom))
      printf("Plane %ld passes at Asia\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[3].geom))
      printf("Plane %ld passes at Europe\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[4].geom))
      printf("Plane %ld passes at North_America\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[5].geom))
      printf("Plane %ld passes at Oceania\n", planes[i].trj_id);
    if (eintersects_tpoint_geo((const Temporal *)planes[i].trip, continents[6].geom))
      printf("Plane %ld passes at South_America\n", planes[i].trj_id);   
  }
}



int main(int argc, char **argv)
{
  plane_point_record rec;
  area_record continents[7] = {0};
  int no_records = 0;
  int no_nulls = 0;
  //int no_planeBB = 0;
  int no_writes = 0;
  const Interval *maxt = pg_interval_in("1 day", -1);
  char point_buffer[MAX_LENGTH_POINT];
  char text_buffer[MAX_LENGTH_HEADER];
  /* Allocate space to build the planes trip */
  trip_record planes[MAX_TRIPS] = {0};
  /* Number of planes */
  int num_planes = 0;
  /* Iterator variable */
  int i;
  /* Return value */
  int return_value = 0;

  /***************************************************************************
   * Section 1: Create regions
   ***************************************************************************/
  
  const STBox *Africa = stbox_in("SRID=4326;STBOX X((-17.625, -34.833), (51.5, 37.5))");
  const STBox *Antarctica = stbox_in("SRID=4326;STBOX X((-180, -90), (180, -60))");
  const STBox *Asia = stbox_in("SRID=4326;STBOX X((26.0, -10.0), (180.0, 81.0))");
  const STBox *Europe = stbox_in("SRID=4326;STBOX X((-31.5, 34.5), (39.0, 81.0))");
  const STBox *North_America = stbox_in("SRID=4326;STBOX X((-168.0, 7.2), (-25.0, 83.2))");
  const STBox *Oceania = stbox_in("SRID=4326;STBOX X((110.0, -50.0), (180.0, 0.0))");
  const STBox *South_America = stbox_in("SRID=4326;STBOX X((-81.0, -56.0), (-34.0, 13.0))");

  continents[0].geom= stbox_to_geo(Africa);
  continents[1].geom = stbox_to_geo(Antarctica);
  continents[2].geom = stbox_to_geo(Asia);
  continents[3].geom = stbox_to_geo(Europe);
  continents[4].geom= stbox_to_geo(North_America);
  continents[5].geom = stbox_to_geo(Oceania);
  continents[6].geom = stbox_to_geo(South_America);

  /***************************************************************************
   * Section 2: Initialize MEOS, open the input CSV file
   ***************************************************************************/

  /* Initialize MEOS */
  meos_initialize(NULL, NULL);

  /* Get start time */
  clock_t t = clock();
  clock_t t1 = clock();

  /* You may substitute the full file path in the first argument of fopen */
  FILE *fileIn = fopen("data/states_2020-05-25-00.csv", "r");
  FILE *file_out = fopen("data/planes_trips.csv", "w+");

  printf("file in opened\n");

  if (!fileIn)
  {
    printf("Error opening input file\n");
    return_value = 1;
    goto cleanup;
  }
  /***************************************************************************
   * Section 3: Read input file line by line and append each observation as a
   * temporal point in MEOS
   ***************************************************************************/
  printf("Accumulating %d instants before sending them to the logfile\n" "(one marker every logfile update)\n", NO_INSTANTS_BATCH);
  /* Read the first line of the file with the headers */
  fscanf(fileIn, "%73s\n", text_buffer);

  /* Continue reading the file */
  do
  {
    int read = fscanf(fileIn,"%19[^,],%ld,%f,%f,%f,%f,%f,%f,%f\n",
											text_buffer, &rec.trj_id, &rec.lat, &rec.lon, &rec.velocity, &rec.heading, &rec.vertrate, &rec.baroaltitude, &rec.geoaltitude);
    /* Transform the string representing the timestamp into a timestamp value */
		//printf("traject %ld, lat %f, lon %f, vel %f, heading %f, vertrate %f, baroaltitude %f, geoaltitude %f\n", rec.trj_id, rec.lat, rec.lon, rec.velocity, rec.heading, rec.vertrate, rec.baroaltitude, rec.geoaltitude);
		rec.T = pg_timestamp_in(text_buffer, -1);

    if (read == 9)
      no_records++;
    if (read != 9 && !feof(fileIn))
    {
      printf("Record with missing values ignored\n");
      no_nulls++;
    }
    if (ferror(fileIn))
    {
      printf("Error reading file\n");
      fclose(fileIn);
      // fclose(fileOut);
    }
    /* Find the place to store the new instant */
    int plane = -1;
    for (i = 0; i < MAX_TRIPS; i++)
    {
      if (planes[i].trj_id == rec.trj_id)
      {
        plane = i;
        break;
      }
    }
    if (plane < 0)
    {
      plane = num_planes++;
      if (plane == MAX_TRIPS)
      {
        printf("The maximum number of planes in the input file is bigger than %d", MAX_TRIPS);
        return_value = 1;
        goto cleanup;
      }

      planes[plane].trj_id = rec.trj_id;
		}
    /*
     * Append the latest observation to the corresponding plane.
     * In the input file it is assumed that
     * - The coordinates are given in the WGS84 geographic coordinate system
     * - The timestamps are given in GMT time zone
     */
    char *t_out = pg_timestamp_out(rec.T);
    sprintf(point_buffer, "SRID=4326;Point(%lf %lf)@%s+00", rec.lon,
            rec.lat, t_out);

    /* Send to the output file the trip if reached the maximum number of instants */
    if (planes[plane].trip && planes[plane].trip->count == NO_INSTANTS_BATCH)
    {
      char *temp_out = tsequence_out(planes[plane].trip, 15);
      //query1(num_planes,planes, continents);
      fprintf(file_out, "%ld, %s\n",planes[plane].trj_id, temp_out);
      /* Free memory */
      free(temp_out);
      no_writes++;
      printf("*");
      fflush(stdout);
      /* Restart the sequence by only keeping the last instants */
      tsequence_restart(planes[plane].trip, NO_INSTANTS_KEEP);
    }
    free(t_out);
    /* Append the last observation */
    TInstant *inst = (TInstant *)tgeompoint_in(point_buffer);
    if (!planes[plane].trip)
      planes[plane].trip = tsequence_make_exp((const TInstant **)&inst, 1,
                                             NO_INSTANTS_BATCH, true, true, LINEAR, false);
    else
      tsequence_append_tinstant(planes[plane].trip, inst, 1000, maxt, true);

  } while (!feof(fileIn));

	// for (size_t i = 0; i < num_planes; i++)
  // {
  //   /* Transform the trip from SRID 4326 to a different SRID */
  //   Temporal *trip_transformed = tpoint_transform((const Temporal *)planes[i].trip, 3414);
  //   planes[i].trip = (TSequence *)trip_transformed;
  // }

  t1 = clock() - t1;
  double time_taken1 = ((double)t1) / CLOCKS_PER_SEC;
  printf("\nThe program took %f seconds to read and create trajectories\n", time_taken1);
  printf("%d records read.\n%d incomplete records ignored.\n%d planes\n", no_records, no_nulls, num_planes);

  /***************************************************************************
   * Section 4: Range Queries
   ***************************************************************************/

  /* 1- List planes that passes at a region */
  printf("1- List planes that passes at a region\n");
  query1(num_planes,planes, continents);

  /* 2- List planes that were within a region from Regions during a period from Periods */
  printf("2- List planes that were within a region from Regions during a period from Periods\n");



/* Clean up */
cleanup:
  /* Free memory */
  for (i = 0; i < num_planes; i++)
    free(planes[i].trip);
  /* Finalize MEOS */
  meos_finalize();
  /* Close the connection to the logfile */
  fclose(fileIn);
  return return_value;
}