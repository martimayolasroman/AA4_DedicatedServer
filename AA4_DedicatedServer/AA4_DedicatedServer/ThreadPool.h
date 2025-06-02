#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional> // Para std::function
#include <atomic>     // Para std::atomic<bool>
#include <iostream>


class ThreadPool
{

public:
	
	ThreadPool(size_t num_threads);
	~ThreadPool();

	template<typename F, typename... Args>
	void enqueueTask(F&& func, Args&&... args);
	void stop();

private:

	void workerLoop();
	size_t num_threads_;
	std::vector<std::thread> workers_;
	std::queue<std::function<void()>> tasks_queue_;

	std::mutex queue_mutex_;
	std::condition_variable condition_variable_; // Para que los workers esperen si la cola está vacía
	std::atomic<bool> stop_flag_;






};

template<typename F, typename ...Args>
inline void ThreadPool::enqueueTask(F&& func, Args && ...args)
{

	auto task = std::bind(std::forward<F>(func), std::forward<Args>(args)...);

	{

		std::lock_guard<std::mutex> lock(queue_mutex_);
		if (stop_flag_) {
			return;
		}
		tasks_queue_.emplace(std::move(task));// Añade la tarea a la cola

	}

	condition_variable_.notify_one();// Despierta a un worker que esté esperando
}
