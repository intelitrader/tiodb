/*
Tio: The Information Overlord
Copyright 2010 Rodrigo Strauss (http://www.1bit.com.br)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#pragma once

#include <thread>
#include <mutex>

namespace tio {
	namespace MemoryStorage
{
	using std::make_tuple;
	using tio::tio_recursive_mutex;

	class VectorStorage : 
		boost::noncopyable,
		public std::enable_shared_from_this<VectorStorage>,
		public ITioStorage
	{
	private:

		typedef vector<ValueAndMetadata> DataContainerT;
		DataContainerT data_;
		string name_, type_;
		EventSink sink_;

		uint64_t revNum_;

		typedef std::lock_guard<std::mutex> lock_guard_t;
		std::mutex mutex_;

		void Publish(ContainerEventCode eventCode, const TioData& k, const TioData& v, const TioData& m)
		{
			decltype(sink_) sink;
			
			{
				lock_guard_t lock(mutex_);

				if (!sink_)
					return;

				sink = sink_;				
			}

			sink(GetId(), eventCode, k, v, m);
		}

		inline ValueAndMetadata& GetInternalRecord(const TioData& key)
		{
			return data_.at(GetRecordNumber(key));
		}

		inline ValueAndMetadata& GetInternalRecord(const TioData* key)
		{
			return GetInternalRecord(*key);
		}

		inline size_t GetRecordNumber(int index)
		{
			//
			// python like index (-1 for last, -2 for before last, so on)
			//
			if(index < 0)
			{
				if(-index > (int)data_.size())
					throw std::invalid_argument("invalid subscript");
				index = data_.size() + index;
			}

			return static_cast<size_t>(index);
		}

		inline size_t GetRecordNumber(const TioData& td)
		{
			return GetRecordNumber(td.AsInt());
		}

		inline size_t GetRecordNumber(const TioData* td)
		{
			return GetRecordNumber(*td);
		}


	public:

		VectorStorage(const string& name, const string& type) 
			: name_(name)
			, type_(type)
			, revNum_(0)
		  {}

		  ~VectorStorage()
		  {
			  return;
		  }

		  virtual uint64_t GetRevNum()
		  {
			  lock_guard_t lock(mutex_);
			  return revNum_;
		  }

		  virtual uint64_t GetId()
		  {
			  return reinterpret_cast<uint64_t>(this);
		  }

		  virtual string GetName()
		  {
			  lock_guard_t lock(mutex_);
			  return name_;
		  }

		  virtual string GetType()
		  {
			  return type_;
		  }

		  virtual string Command(const string& command)
		  {
			  throw std::invalid_argument("command not supported");
		  }

		  virtual size_t GetRecordCount()
		  {
			  lock_guard_t lock(mutex_);
			  return data_.size();
		  }

		  virtual void PushBack(const TioData& key, const TioData& value, const TioData& metadata)
		  {
				CheckValue(value);

				{
					lock_guard_t lock(mutex_);
					data_.push_back(ValueAndMetadata(value, metadata));
				}

				Publish(EVENT_CODE_INSERT, static_cast<int>(data_.size() - 1), value, metadata);
		  }

		  virtual void PushFront(const TioData& key, const TioData& value, const TioData& metadata)
		  {
			  CheckValue(value);

			  {
				lock_guard_t lock(mutex_);
			  	data_.insert(data_.begin(), ValueAndMetadata(value, metadata));
			  }

			  Publish(EVENT_CODE_INSERT, 0, value, metadata);
		  }

	private:
		void _Pop(vector<ValueAndMetadata>::iterator i, TioData* value, TioData* metadata)
		{
			ValueAndMetadata& data = *i;

			if(value)
				*value = data.value;

			if(metadata)
				*metadata = data.metadata;

			data_.erase(i);
		}
	public:

		virtual void PopBack(TioData* key, TioData* value, TioData* metadata)
		{
			if(data_.empty())
				throw std::invalid_argument("empty");

			if(key)
				*key = static_cast<int>(data_.size() - 1);

			{
				lock_guard_t lock(mutex_);
				_Pop(data_.end() - 1, value, metadata);
			}

			Publish(EVENT_CODE_DELETE,
				key ? *key : TIONULL, 
				value ? *value : TIONULL,
				metadata ? *metadata : TIONULL);
		}

		virtual void PopFront(TioData* key, TioData* value, TioData* metadata)
		{
			if(data_.empty())
				throw std::invalid_argument("empty");

			{
				lock_guard_t lock(mutex_);
				_Pop(data_.begin(), value, metadata);
			}

			Publish(EVENT_CODE_DELETE,
				0, 
				value ? *value : TIONULL,
				metadata ? *metadata : TIONULL);
		}

		void CheckValue(const TioData& value)
		{
			if(value.Empty())
				throw std::invalid_argument("value??");
		}


		virtual void Set(const TioData& key, const TioData& value, const TioData& metadata)
		{
			CheckValue(value);

			{
				lock_guard_t lock(mutex_);
				ValueAndMetadata& data = GetInternalRecord(key);
				data = ValueAndMetadata(value, metadata);
			}

			Publish(EVENT_CODE_SET, key, value, metadata);
		}

		virtual void Insert(const TioData& key, const TioData& value, const TioData& metadata)
		{
			CheckValue(value);

			{
				lock_guard_t lock(mutex_);
				size_t recordNumber = GetRecordNumber(key);

				// check out of bounds
				GetRecord(key, NULL, NULL, NULL);

				data_.insert(data_.begin() + recordNumber, ValueAndMetadata(value, metadata));
			}

			Publish(EVENT_CODE_INSERT, key, value, metadata);
		}

		virtual void Delete(const TioData& key, const TioData& value, const TioData& metadata)
		{
			size_t recordNumber = GetRecordNumber(key);

			{
				lock_guard_t lock(mutex_);

				// check out of bounds
				GetRecord(key, NULL, NULL, NULL);

				data_.erase(data_.begin() + recordNumber);
			}

			Publish(EVENT_CODE_DELETE, key, value, metadata);
		}

		virtual void Clear()
		{
			{
				lock_guard_t lock(mutex_);
				data_.clear();
			}

			Publish(EVENT_CODE_CLEAR, TIONULL, TIONULL, TIONULL);
		}

		virtual shared_ptr<ITioResultSet> Query(int startOffset, int endOffset, const TioData& query)
		{
			if(!query.IsNull())
				throw std::runtime_error("query type not supported by this container");

			DataContainerT::const_iterator begin, end;

			//
			// if client is asking for a negative index that's bigger than the container,
			// will start from beginning. Ex: if container size is 3 and start = -5, will start from 0
			//
			{
				lock_guard_t lock(mutex_);
				if(GetRecordCount() == 0)
				{
					begin = end = data_.end();
					startOffset = 0;
				}
				else
				{
					int recordCount = GetRecordCount();

					NormalizeQueryLimits(&startOffset, &endOffset, recordCount);

				}

				VectorResultSet::ContainerT resultSetItems;

				resultSetItems.reserve(endOffset - startOffset);

				for(int key = startOffset; key != endOffset ; ++key)
					resultSetItems.push_back(make_tuple(TioData(key), data_[key].value, data_[key].metadata));

				return shared_ptr<ITioResultSet>(
					new VectorResultSet(std::move(resultSetItems), TIONULL));
			}
		}

		virtual void GetRecord(const TioData& searchKey, TioData* key, TioData* value, TioData* metadata)
		{	
			lock_guard_t lock(mutex_);
			const ValueAndMetadata& data = GetInternalRecord(searchKey);

			if(key)
				*key = searchKey;

			if(value)
				*value = data.value;

			if(metadata)
				*metadata = data.metadata;
		}

		virtual void SetSubscriber(EventSink sink)
		{
			lock_guard_t lock(mutex_);
			sink_ = sink;
		}

		virtual void RemoveSubscriber()
		{
			lock_guard_t lock(mutex_);
			sink_ = nullptr;
		}

/*
		virtual unsigned int Subscribe(EventSink sink, const string& start)
		{
			unsigned int cookie = 0;
			size_t startIndex = 0;
			
			if(!start.empty())
			{
				try
				{
					startIndex = GetRecordNumber(lexical_cast<int>(start));

					//
					// if client asks to start at index 0, it's never
					// an error, even if the vector is empty. So, we'll
					// not test out of bounds if zero
					//
					if(startIndex != 0)
						data_.at(startIndex); 
				}
				catch(std::exception&)
				{
					throw std::invalid_argument("invalid start index");
				}
			}

			cookie = dispatcher_.Subscribe(sink);

			if(start.empty())
				return cookie;
			
			//
			// key is the start index to send
			//
			for(size_t x = startIndex ; x < data_.size() ; x++)
			{
				const ValueAndMetadata& data = data_[x];
				sink("push_back", (int)x, data.value, data.metadata);
			}

			return cookie;
		}
		virtual void Unsubscribe(unsigned int cookie)
		{
			dispatcher_.Unsubscribe(cookie);
		}
		*/
	};
}}
