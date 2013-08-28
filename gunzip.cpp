#include <iostream>
#include <fstream>
#include <algorithm>
#include <thread>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
// c headers
#include <stdio.h>
#include <sys/types.h>
#include <utime.h>

using namespace std;

void gunzip( char* file_name)
{
  using namespace boost::iostreams;
  string tmp_file_name = string(file_name) + ".tmp";
  ifstream ifile(file_name,ios_base::in | ios_base::binary);
  filtering_streambuf<input> in;
  in.push(gzip_decompressor());
  in.push(ifile);
  ofstream ofile( tmp_file_name );
  boost::iostreams::copy(in, ofile);
  ofile.close();
  ifile.close();
  time_t mtime =  in.component<0,gzip_decompressor>()->mtime();
  string orig_file_name = in.component<0,gzip_decompressor>()->file_name();
  rename( tmp_file_name.c_str(), 
          orig_file_name.c_str()
        );
  struct utimbuf ut;
  ut.modtime=mtime;
  ut.actime=mtime;
  utime(orig_file_name.c_str(),&ut);
  cout << orig_file_name << endl;
}



int main( int argc, char* argv[] )
{

  if(argc==1) return 0;

  list<thread> threads;
  for_each( argv+1, argv+argc, 
            [ &threads ]( char* elem )
              { 
                threads.push_back( thread( bind( gunzip, elem ) ) );
              } 
          );


  for_each( threads.begin(),
            threads.end(),
            []( thread& elem ){ elem.join(); } 
          );

}

