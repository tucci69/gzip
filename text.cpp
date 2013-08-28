#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>
#include <utime.h>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <thread>
#include <iostream>
#include <list>

using namespace std;

unsigned lines       = 10;
const unsigned length      = 62;
const unsigned rand_length = 40;
unsigned num_files   = 1;

const char charset[] =
  "0123456789"
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz";
const unsigned max_index = (sizeof(charset) - 1);
string str1 = charset;

void make_file(string  file_name)
{
  ofstream file(file_name) ;
  string str2(rand_length,0);
  for(unsigned k=0, j=0; k<lines; k++)
  {
    generate_n(
                str2.begin(), 
                rand_length, 
                []() -> char { return charset[ rand() % max_index ]; } 
              );
    file << setfill('0') << setw(7) << k 
      << str1.substr(j) << str1.substr(0,j) << str2 <<  endl;
    j = ++j == length ? 0 : j ; 
  }
  file.close();
  struct stat st;
  stat( file_name.c_str(), &st );

  struct utimbuf ut;
  time_t year = 365 * 24 * 60 * 60;
  ut.actime  = st.st_atime - year;
  ut.modtime = st.st_mtime - year;
  utime( file_name.c_str(), &ut );
}


int main( int argc, char* argv[] )
{

  bool multi=false;

  for(int j = 0; j < argc; j++)
  {
    if(j==1) stringstream( argv[j] )  >> num_files ;
    if(j==2) stringstream( argv[j] )  >> lines;
    if(j==3) stringstream( argv[j] )  >> multi;
  }

  cout << "Number of files "    << num_files << endl;
  cout << "Number of lines "    << lines     << endl;
  cout << "Is multi threading " << multi     << endl;


  list<thread> t_pool(num_files);

  if(multi)
    cout << "Run in multi mode" << endl;
  else
    cout << "Run in single mode" << endl;

  ostringstream os;
  int i = 1;
  for_each( t_pool.begin(), 
            t_pool.end(),
            [ &os, &multi, &i ] (thread& elem) mutable
            {
              os.str(string());
              os << "stream" << setfill('0') << setw(2) << i++ <<  ".txt";
              elem = thread( bind(make_file, os.str()) ) ;
              if (!multi)
                elem.join();
            }
          );
  if(multi)
    for_each( t_pool.begin(), 
              t_pool.end(), 
              [](thread& elem) { elem.join(); } 
            );


}
