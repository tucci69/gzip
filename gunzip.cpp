#include <iostream>
#include <fstream>
#include <algorithm>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

using namespace std;

void gunzip( char* file_name)
{
  using namespace boost::iostreams;
  cout << file_name << endl;
  string tmp_file_name = string(file_name) + ".tmp";
  ifstream ifile(file_name,ios_base::in | ios_base::binary);
  filtering_streambuf<input> in;
  in.push(gzip_decompressor());
  in.push(ifile);
  ofstream ofile( tmp_file_name );
  boost::iostreams::copy(in, ofile);
  ofile.close();
  cout << in.component<0,gzip_decompressor>()->mtime() << endl;
  cout << in.component<0,gzip_decompressor>()->file_name() << endl;
  rename( tmp_file_name.c_str(), 
          (in.component<0,gzip_decompressor>()->file_name()).c_str()
        );
}



int main( int argc, char* argv[] )
{

  if(argc==1) return 0;
  for_each( argv+1, argv+argc, 
            [](char* elem ){ gunzip(elem) ; } 
          );

}

