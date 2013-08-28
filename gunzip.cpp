#include <iostream>
#include <fstream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

using namespace std;

void gunzip( char* file_name)
{
  cout << file_name << endl;
  ifstream file(file_name,ios_base::in | ios_base::binary);

}



int main( int argc, char* argv[] )
{

  if(argc==1) return 0;

  for(int i=1; i<argc; i++)
  {
    gunzip(argv[i]);
  }

}

