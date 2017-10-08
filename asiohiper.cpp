/*
 * This program is in c++ and uses boost::asio instead of libevent/libev.
 * Requires boost::asio, boost::bind and boost::system
 *
 * This is an adaptation of libcurl's "hiperfifo.c" and "evhiperfifo.c"
 * sample programs. This example implements a subset of the functionality from
 * hiperfifo.c, for full functionality refer hiperfifo.c or evhiperfifo.c
 *
 * Written by Lijo Antony based on hiperfifo.c by Jeff Pohlmeyer
 *
 * When running, the program creates an easy handle for a URL and
 * uses the curl_multi API to fetch it.
 *
 * Note:
 *  For the sake of simplicity, URL is hard coded to "www.google.com"
 *
 * This is purely a demo app, all retrieved data is simply discarded by the
 * write callback.
 */

#include <iostream>
#include <string>
#include <map>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

#include <curl/curl.h>

using namespace std;

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */

/* boost::asio related objects
 * using global variables for simplicity
 */
boost::asio::io_service io_service;
boost::asio::deadline_timer timer(io_service);
std::map<curl_socket_t, boost::shared_ptr<boost::asio::ip::tcp::socket>> socket_map;

/* Global information, common to all connections */
struct GlobalInfo
{
  CURLM *multi       = nullptr;
  int still_running  = 0;
};

/* Information associated with a specific easy handle */
struct ConnInfo
{
  CURL *easy         = nullptr;
  string url;
  GlobalInfo *global = nullptr;
  char error[CURL_ERROR_SIZE];
};

static void timer_cb(const boost::system::error_code & error, GlobalInfo *g);

/* Update the event timer after curl_multi library calls */
static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g)
{
  cout << endl <<  boost::format{"multi_timer_cb: timeout_ms %1%"} % timeout_ms;

  /* cancel running timer */
  timer.cancel();

  if(timeout_ms > 0) {
    /* update timer */
    timer.expires_from_now(boost::posix_time::millisec(timeout_ms));
    timer.async_wait([g](const boost::system::error_code& error)
		    {
		    	timer_cb(error, g);
		    }
    );

  }
  else if(timeout_ms == 0) {
    /* call timeout function immediately */
    boost::system::error_code error; /*success*/
    timer_cb(error, g);
  }

  return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
  if(CURLM_OK != code) {
    const char *s;
    switch(code) {
    case CURLM_CALL_MULTI_PERFORM:
      s = "CURLM_CALL_MULTI_PERFORM";
      break;
    case CURLM_BAD_HANDLE:
      s = "CURLM_BAD_HANDLE";
      break;
    case CURLM_BAD_EASY_HANDLE:
      s = "CURLM_BAD_EASY_HANDLE";
      break;
    case CURLM_OUT_OF_MEMORY:
      s = "CURLM_OUT_OF_MEMORY";
      break;
    case CURLM_INTERNAL_ERROR:
      s = "CURLM_INTERNAL_ERROR";
      break;
    case CURLM_UNKNOWN_OPTION:
      s = "CURLM_UNKNOWN_OPTION";
      break;
    case CURLM_LAST:
      s = "CURLM_LAST";
      break;
    default:
      s = "CURLM_unknown";
      break;
    case CURLM_BAD_SOCKET:
      s = "CURLM_BAD_SOCKET";
      fprintf(MSG_OUT, "\nERROR: %s returns %s", where, s);
      /* ignore this error */
      return;
    }

    fprintf(MSG_OUT, "\nERROR: %s returns %s", where, s);

    exit(code);
  }
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  boost::shared_ptr<ConnInfo> conn;
  CURL *easy;
  CURLcode res;

  fprintf(MSG_OUT, "\nREMAINING: %d", g->still_running);

  while((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      fprintf(MSG_OUT, "\nDONE: %s => (%d) %s", eff_url, res, conn->error);
      curl_multi_remove_handle(g->multi, easy);
      curl_easy_cleanup(easy);
      conn.reset();
    }
  }
}

/* Called by asio when there is an action on a socket */
static void event_cb(GlobalInfo *g, curl_socket_t s,
                     int action, const boost::system::error_code & error,
                     int *fdp)
{
  fprintf(MSG_OUT, "\nevent_cb: action=%d", action);

  if(socket_map.find(s) == socket_map.end()) {
    fprintf(MSG_OUT, "\nevent_cb: socket already closed");
    return;
  }

  /* make sure the event matches what are wanted */
  if(*fdp == action || *fdp == CURL_POLL_INOUT) {
    CURLMcode rc;
    if(error)
      action = CURL_CSELECT_ERR;
    rc = curl_multi_socket_action(g->multi, s, action, &g->still_running);

    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(g);

    if(g->still_running <= 0) {
      fprintf(MSG_OUT, "\nlast transfer done, kill timeout");
      timer.cancel();
    }

    /* keep on watching.
     * the socket may have been closed and/or fdp may have been changed
     * in curl_multi_socket_action(), so check them both */
    if(!error && socket_map.find(s) != socket_map.end() &&
       (*fdp == action || *fdp == CURL_POLL_INOUT)) {
      auto tcp_socket = socket_map.find(s)->second;

      if(action == CURL_POLL_IN) {
        tcp_socket->async_read_some(
			boost::asio::null_buffers(),
			[g, s, action, fdp] 
			(const boost::system::error_code& error, const size_t)
			{
				event_cb(g, s, action, error, fdp);
			});
      }
      if(action == CURL_POLL_OUT) {
        tcp_socket->async_write_some(
			boost::asio::null_buffers(),
                        [g, s, action, fdp]
			(const boost::system::error_code& error, const size_t)
			{
                        	event_cb(g, s, action, error, fdp);
			});
      }
    }
  }
}

/* Called by asio when our timeout expires */
static void timer_cb(const boost::system::error_code & error, GlobalInfo *g)
{
  if(!error) {
    fprintf(MSG_OUT, "\ntimer_cb: ");

    CURLMcode rc;
    rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0,
                                  &g->still_running);

    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(g);
  }
}

/* Clean up any data */
static void remsock(int *f, GlobalInfo *g)
{

  if(f) {
    cout << endl << "remsock: " << *f;
    free(f);
  }
}

static void setsock(int *fdp, curl_socket_t s, CURL *e, int act, int oldact,
                    GlobalInfo *g)
{
  fprintf(MSG_OUT, "\nsetsock: socket=%d, act=%d, fdp=%p", s, act, fdp);

  auto it = socket_map.find(s);

  if(it == socket_map.end()) {
    fprintf(MSG_OUT, "\nsocket %d is a c-ares socket, ignoring", s);
    return;
  }

  auto tcp_socket = it->second;

  *fdp = act;

  if(act == CURL_POLL_IN) {
    fprintf(MSG_OUT, "\nwatching for socket to become readable");
    if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
	    tcp_socket->async_read_some(
			    boost::asio::null_buffers(),
			    [g, s, fdp]
			    (const boost::system::error_code& error, const size_t)
			    {
			    	event_cb(g, s, CURL_POLL_IN, error, fdp);
			    });
    }
  }
  else if(act == CURL_POLL_OUT) {
    fprintf(MSG_OUT, "\nwatching for socket to become writable");
    if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_write_some(
		      boost::asio::null_buffers(),
			    [g, s, fdp]
			    (const boost::system::error_code& error, const size_t)
			    {
			    	event_cb(g, s, CURL_POLL_OUT, error, fdp);
			    });
    }
  }
  else if(act == CURL_POLL_INOUT) {
    fprintf(MSG_OUT, "\nwatching for socket to become readable & writable");
    if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_read_some(
              boost::asio::null_buffers(),
              [g, s, fdp](const boost::system::error_code& error, const size_t)
              {
                    event_cb(g, s, CURL_POLL_IN, error, fdp);
              });
    }
    if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_write_some(
              boost::asio::null_buffers(),
              [g, s, fdp](const boost::system::error_code& error, const size_t)
              {
                    event_cb(g, s, CURL_POLL_OUT, error, fdp);
              });
    }
  }
}

static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
  /* fdp is used to store current action */
  int *fdp = (int *) calloc(sizeof(int), 1);

  setsock(fdp, s, easy, action, 0, g);
  curl_multi_assign(g->multi, s, fdp);
}

static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  cout << endl <<
     boost::format{"sock_cb: socket=%1%, what=%2%, sockp=%3% "} % s % what % sockp;

  GlobalInfo *g = (GlobalInfo*) cbp;
  int *actionp = (int *) sockp;
  const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

  cout <<
          boost::format{"socket callback: s=%1% e=%2% what=%3%"} % s % e % whatstr[what];

  if(what == CURL_POLL_REMOVE) {
    cout << endl;
    remsock(actionp, g);
  }
  else {
      if(!actionp) {
          fprintf(MSG_OUT, "\nAdding data: %s", whatstr[what]);
          addsock(s, e, what, g);
      }
      else {
          cout << endl << 
              boost::format{"Changing action from %1% to %2%"} %
              whatstr[*actionp] % whatstr[what];
          setsock(actionp, s, e, what, *actionp, g);
      }
  }

  return 0;
}

/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t written = size * nmemb;
  string buffer((const char*)ptr, written);
  cout << endl << "===== write_cb ======" << endl;
  cout << buffer;
  cout << "=====================" << endl;
  return written;


}

/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb(void *p, double dltotal, double dlnow, double ult,
                   double uln)
{
  ConnInfo *conn = (ConnInfo *)p;

  (void)ult;
  (void)uln;

  fprintf(MSG_OUT, "\nProgress: %s (%g/%g)", conn->url, dlnow, dltotal);
  fprintf(MSG_OUT, "\nProgress: %s (%g)", conn->url, ult);

  return 0;
}

/* CURLOPT_OPENSOCKETFUNCTION */
static curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                struct curl_sockaddr *address)
{
  fprintf(MSG_OUT, "\nopensocket :");

  curl_socket_t sockfd = CURL_SOCKET_BAD;

  /* restrict to IPv4 */
  if(purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET) {
    /* create a tcp socket object */
    auto tcp_socket = boost::make_shared<boost::asio::ip::tcp::socket>(io_service);

    /* open it and get the native handle*/
    boost::system::error_code ec;
    tcp_socket->open(boost::asio::ip::tcp::v4(), ec);

    if(ec) {
      /* An error occurred */
      cout << std::endl << "Couldn't open socket [" << ec << "][" <<
        ec.message() << "]";
      cout << endl << "ERROR: Returning CURL_SOCKET_BAD to signal error";
    }
    else {
      sockfd = tcp_socket->native_handle();
      fprintf(MSG_OUT, "\nOpened socket %d", sockfd);

      /* save it for monitoring */
      socket_map.insert(make_pair(sockfd, tcp_socket));
    }
  }

  return sockfd;
}

/* CURLOPT_CLOSESOCKETFUNCTION */
static int close_socket(void *clientp, curl_socket_t item)
{

    cout << endl << "close_socket : " << (int)item;
    auto it = socket_map.find(item);

    if(it != socket_map.end()) {
        socket_map.erase(it);
    }

    return 0;
}

/* Create a new easy handle, and add it to the global curl_multi */
static void new_conn(string url, GlobalInfo *g)
{
  CURLMcode rc;

  auto conn = boost::make_shared<ConnInfo>();

  conn->easy = curl_easy_init();
  if(!conn->easy) {
    cout << endl << "curl_easy_init() failed, exiting!";
    exit(2);
  }

  conn->global = g;
  conn->url = url;
  curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url.c_str());
  curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, &conn);
  curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
  curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);

  /* call this function to get a socket */
  curl_easy_setopt(conn->easy, CURLOPT_OPENSOCKETFUNCTION, opensocket);

  /* call this function to close a socket */
  curl_easy_setopt(conn->easy, CURLOPT_CLOSESOCKETFUNCTION, close_socket);

 cout << endl << 
          "Adding easy " << conn->easy <<
          " to multi " << g->multi <<
          " ("<< url << ")";
  rc = curl_multi_add_handle(g->multi, conn->easy);
  mcode_or_die("new_conn: curl_multi_add_handle", rc);

  /* note that the add_handle() will set a time-out to trigger very soon so
     that the necessary socket_action() call will be called by this app */
}

int main(int argc, char **argv)
{

  GlobalInfo g;

  g.multi = curl_multi_init();

  curl_multi_setopt(g.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(g.multi, CURLMOPT_SOCKETDATA, &g);
  curl_multi_setopt(g.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  curl_multi_setopt(g.multi, CURLMOPT_TIMERDATA, &g);

  new_conn("www.google.com", &g);  /* add a URL */

  /* enter io_service run loop */
  io_service.run();

  curl_multi_cleanup(g.multi);

  cout << endl << "done." << endl;

  return 0;
}
