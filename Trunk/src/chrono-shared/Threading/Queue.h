/*
 * Chrono Emulator
 * Copyright (C) 2010 ChronoEmu Team <http://www.forsakengaming.com/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef FQUEUE_H
#define FQUEUE_H

#include "Condition.h"
#include "Mutex.h"
 
template<class T> 
class FQueue 
{
public:
	CHRONO_INLINE FQueue() : cond(&lock) {first=last=nullptr;size=0;}
	volatile unsigned int size;

	uint32 get_size()
	{
		uint32 ret;
		cond.BeginSynchronized();
		ret = size;
		cond.EndSynchronized();
		return ret;
	}

	void push(T &item)
	{
		h*p=new h;
		p->value=item;
		p->pNext=nullptr;
		
		//lock.Acquire();
		cond.BeginSynchronized();
		if(last)//have some items
		{
			last->pNext=p;
			last=p;
			size++;
		}
		else//first item
		{
			last=first=p;
			size=1;
			cond.Signal();
		}
		//lock.Release();
		cond.EndSynchronized();
	}

	T pop_nowait()
	{
		//lock.Acquire();
		cond.BeginSynchronized();
		if(size==0)
		{
			cond.EndSynchronized();
			return nullptr;
		}

		h*tmp=first;
		if(tmp == nullptr)
		{
			cond.EndSynchronized();
			return nullptr;
		}

		if(--size)//more than 1 item
		{
			first=(h*)first->pNext;
		}
		else//last item
		{
			first=last=nullptr;
		}
		//lock.Release();
		cond.EndSynchronized();

		T returnVal = tmp->value;
		delete tmp;

		return returnVal;
	}

	T pop()
	{
		//lock.Acquire();
		cond.BeginSynchronized();
		if(size==0)
		cond.Wait();

		h*tmp=first;
		if(tmp == nullptr)
		{
			cond.EndSynchronized();
			return nullptr;
		}

		if(--size)//more than 1 item
		{
			first=(h*)first->pNext;
		}
		else//last item
		{
			first=last=nullptr;
		}
		//lock.Release();
		cond.EndSynchronized();

		T returnVal = tmp->value;
		delete tmp;
		
		return returnVal;
	}	

	CHRONO_INLINE Condition& GetCond() { return cond; }
	
private:
	struct h
	{
		T value;
		void *pNext;
	};

	h*first;
	h*last;
	
	Mutex lock;
	Condition cond;

};

#endif 


