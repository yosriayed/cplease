#ifndef __task_base_H__
#define __task_base_H__

#include <stop_token>

class packaged_task_base
{
  public:
  virtual ~packaged_task_base()                       = default;
  virtual void run(std::stop_token thread_stop_token) = 0;
};


#endif // __task_base_H__