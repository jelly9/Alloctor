#ifndef __ALLOC_H__
#define __ALLOC_H__
#pragma

#include <new>
typedef void(*HandlerFunc)();

//一级空间配置器
template<int inst>
class __MallocAllocTemplate{
public:
	static void *OOM_Malloc(size_t size)
	{
		while (1){
			if (_handler == NULL){
				throw bad_alloc();
			}
			_handler();//循环调用句柄函数，企图释放一点点内存

			void *ret = malloc(size);
			if (ret)
				return ret;
		}
	}

	void* OOM_Realloc(void *p, size_t n)
	{
		while (1){
			if (0 == _handler) {
				throw bad_alloc();
			}

			_handler();//循环调用句柄函数，企图释放一点点内存
			void *ret = realloc(p, n);
			if (ret) 
				return(ret);
		}
	}

	static void * Allocate(size_t n)
	{
		void *ret = malloc(n);//如果能申请到则直接返回给用户
		if (0 == ret) 
			ret = OOM_Malloc(n);//申请不到调用内存不足处理例程
		return ret;
	}

	static void Deallocate(void *p)
	{
		free(p);//直接释放内存，对free的封装
	}

	static void * Reallocate(void *p, size_t new_sz)
	{
		void * ret = realloc(p, new_sz);//对realloc的简单封装
		if (0 == ret) 
			ret = OOM_Realloc(p, new_sz);
		return ret;
	}

	//参数是函数指针，返回值是函数指针
	static HandlerFunc SetMallocHandler(HandlerFunc f)
	{
		HandlerFunc old = _handler;
		_handler = f;
		return (old);
	}

private:
	static void *OOM_Malloc(size_t);
	static void *OOM_Realloc(void *, size_t);
	static HandlerFunc _handler;//句柄函数
};

HandlerFunc __MallocAllocTemplate<0>::_handler = 0;

template<bool threads, int inst>
class __DefaultAllocTemplate{
public:
private:
	static void * Allocate(size_t n)
	{
		if (n > (size_t)__MAX_BYTES) {
			return __MallocAllocTemplate<0>::Allocate(n);
		}
		size_t index = FREELIST_INDEX(n);
		if (_freeList[index]){
			obj *ret = _freeList[index];
			_freeList[index] = ret->_freeListLink;
			return ret;
		}
		else{
			return Refill(ROUND_UP(n));
		}
	};

	static void Deallocate(void *p, size_t n)
	{
		obj *q = (obj *)p;
		obj ** my_free_list;

		if (n > (size_t)__MAX_BYTES) {
			__MallocAllocTemplate<0>::deallocate(p, n);
			return;
		}

		size_t index = FREELIST_INDEX(n);

		p->_freeListLink = _freeList[index];
		_freeList[index] = p;
	}

	//获取对应节点的下标
	static  size_t FREELIST_INDEX(size_t bytes) {
		return (((bytes)+__ALIGN - 1) / __ALIGN - 1);
	}

	//将bytes向上调整至8的倍数
	static size_t ROUND_UP(size_t bytes) {
		return (((bytes)+__ALIGN - 1) & ~(__ALIGN - 1));
	}

	void* Refill(size_t n)
	{
		int nobjs = 20;
		char * chunk = ChunkAlloc(n, nobjs);
		obj ** my_free_list;
		obj * ret;
		obj * current_obj, *next_obj;
		int i;

		if (1 == nobjs)
			return(chunk);
		my_free_list = _freeList + FREELIST_INDEX(n);

		ret = (obj *)chunk;
		*my_free_list = next_obj = (obj *)(chunk + n);
		for (i = 1;; i++) {
			current_obj = next_obj;
			next_obj = (obj *)((char *)next_obj + n);
			if (nobjs - 1 == i) {
				current_obj->_freeListLink = 0;
				break;
			}
			else {
				current_obj->_freeListLink = next_obj;
			}
		}
		return(ret);
	}

	char* ChunkAlloc(size_t size, int& nobjs)
	{
			char * ret;
			size_t total_bytes = size * nobjs;
			size_t bytes_left = _endFree - _startFree;

			if (bytes_left >= total_bytes) {
				ret = _startFree;
				_startFree += total_bytes;
				return(ret);
			}
			else if (bytes_left >= size) {
				nobjs = bytes_left / size;
				total_bytes = size * nobjs;
				ret = _startFree;
				_startFree += total_bytes;
				return(ret);
			}
			else {
				size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);
				if (bytes_left > 0) {
					obj ** my_free_list =
						_freeList + FREELIST_INDEX(bytes_left);

					((obj *)_startFree)->_freeListLink = *my_free_list;
					*my_free_list = (obj *)_startFree;
				}
				_startFree = (char *)malloc(bytes_to_get);
				if (0 == _startFree) {
					int i;
					obj ** my_free_list, *p;
					for (i = size; i <= __MAX_BYTES; i += __ALIGN) {
						my_free_list = _freeList + FREELIST_INDEX(i);
						p = *my_free_list;
						if (0 != p) {
							*my_free_list = p->free_list_link;
							_startFree = (char *)p;
							_endFree = _startFree + i;
							return(ChunkAlloc(size, nobjs));
						}
					}
					_endFree = 0;
					_startFree = (char *)__MallocAllocTemplate<0>::Allocate(bytes_to_get);
				}
				heap_size += bytes_to_get;
				_endFree = _startFree + bytes_to_get;
				return(ChunkAlloc(size, nobjs));
			}
		}

private:
	//尽量用枚举、内联,替换宏
	enum { __ALIGN = 8 };
	//自由链表最大节点
	enum { __MAX_BYTES = 128 };
	//自由链表大小
	enum { __NFREELISTS = __MAX_BYTES / __ALIGN };

	//自由链表的节点类型
	union obj {
		union obj * _freeListLink;
		char client_data[1];
	};
	//自由链表原型
	static union obj *_freeList[__NFREELISTS];

	//狭义内存池的开始和结束标志
	static char *_startFree;
	static char *_endFree;
	static size_t _heapSize;
};
char * __DefaultAllocTemplate<false, 0>::_startFree = 0;
char * __DefaultAllocTemplate<false, 0>::_endFree = 0;
size_t __DefaultAllocTemplate<false, 0>::_heapSize = 0;
//union obj *__DefaultAllocTemplate<false, 0>::_freeList[__NFREELISTS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
typedef __DefaultAllocTemplate<false, 0> Alloc;

#endif 

#if 1
void Test()
{
	int size = 10;
	void *p = __DefaultAllocTemplate<false, 0>::Allocate(size);
	__DefaultAllocTemplate<false, 0>::Deallocate(p, size);
}


#endif