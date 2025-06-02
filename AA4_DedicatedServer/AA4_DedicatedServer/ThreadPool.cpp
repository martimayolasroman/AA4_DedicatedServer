#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t num_threads):num_threads_(num_threads),stop_flag_(false)
{

	if (num_threads_ == 0) {
		num_threads_ = std::thread::hardware_concurrency();
		if (num_threads_ == 0) num_threads_ = 2;
	}

	for (size_t i = 0; i < num_threads_; ++i) {
		workers_.emplace_back(&ThreadPool::workerLoop, this);
	}

}

ThreadPool::~ThreadPool()
{

	stop();



}

void ThreadPool::stop()
{

	if (stop_flag_.exchange(true)) { // Si ya estaba true, no hacer nada (evita múltiples llamadas)
		return;
	}

	condition_variable_.notify_all(); // Despertar a todos los workers para que vean stop_flag_

	for (std::thread& worker_thread : workers_) {
		if (worker_thread.joinable()) {
			worker_thread.join();
		}
	}


}

void ThreadPool::workerLoop()
{

    while (true) {
        std::function<void()> task_to_execute;
        { // Bloque para el unique_lock
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Esperar hasta que haya una tarea o se indique detener
            condition_variable_.wait(lock, [this] {
                return stop_flag_ || !tasks_queue_.empty();
                });

            // Si se debe detener y no hay más tareas, el worker termina
            if (stop_flag_ && tasks_queue_.empty()) {
                return;
            }

            // Tomar la siguiente tarea de la cola
            if (!tasks_queue_.empty()) {
                task_to_execute = std::move(tasks_queue_.front());
                tasks_queue_.pop();
            }
        } // El lock se libera aquí

        // Si se tomó una tarea, ejecutarla
        if (task_to_execute) {
            try {
                task_to_execute();
            }
            catch (const std::exception& e) {
                std::cerr << "ThreadPool: Exception caught in task: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "ThreadPool: Unknown exception caught in task." << std::endl;
            }
        }
    }




}
