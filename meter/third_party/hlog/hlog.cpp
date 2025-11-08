// hlog.cpp : 定义静态库的函数。
#include "hlog.h"

Hlog::OutMode Hlog::GetOutModeEnum(const std::string& strMode)
{
	OutMode eMode;
	if (strMode == "SYNC")
	{
		eMode = OutMode::SYNC;
	}
	else if (strMode == "ASYNC")
	{
		eMode = OutMode::ASYNC;
	}
	else
	{
		eMode = OutMode::SYNC;
	}

	return eMode;
}

Hlog::OutLevel Hlog::GetOutLevelEnum(const std::string& strLevel)
{
	OutLevel eLevel;

	if (strLevel == "TRACE")
	{
		eLevel = OutLevel::LEVEL_TRACE;
	}
	else if (strLevel == "DEBUG")
	{
		eLevel = OutLevel::LEVEL_DEBUG;
	}
	else if (strLevel == "INFO")
	{
		eLevel = OutLevel::LEVEL_INFO;
	}
	else if (strLevel == "WARN")
	{
		eLevel = OutLevel::LEVEL_WARN;
	}
	else if (strLevel == "ERROR")
	{
		eLevel = OutLevel::LEVEL_ERROR;
	}
	else if (strLevel == "CRITI")
	{
		eLevel = OutLevel::LEVEL_CRITI;
	}
	else
	{
		eLevel =  OutLevel::LEVEL_TRACE;
	}

	return eLevel;
}

Hlog::Hlog()
	:m_bInit(false)
{

}

Hlog::~Hlog()
{
	if (m_bInit)
	{
		this->UnInit();
	}
}

bool Hlog::AddColorConsole(const char* pLoggerName, const OutLevel level)
{
	printf("[log]: AddColorConsole, logName=%s  level=%d  \n", pLoggerName, level);
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level((spdlog::level::level_enum)level);
	//console_sink->set_pattern(LOG_OUTPUT_FORMAT);
	UpdateSinkMap(pLoggerName, console_sink);
	return true;
}

bool Hlog::AddRotatingFile(const char* pLoggerName, const char* pFileName, const int nMaxFileSize, const int nMaxFile, const OutLevel level)
{
	printf("[log]: AddRotatingFile, logName=%s  level=%d  fileName=%s  maxFileSize=%d  maxFile=%d \n", pLoggerName, level, pFileName, nMaxFileSize, nMaxFile);
	auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(pFileName, nMaxFileSize, nMaxFile);
	file_sink->set_level((spdlog::level::level_enum)level);
	//file_sink->set_pattern(LOG_OUTPUT_FORMAT);
	UpdateSinkMap(pLoggerName, file_sink);
	return true;
}

bool Hlog::AddDailyFile(const char* pLoggerName, const char* pFileName, const int nHour, const int nMinute, const OutLevel eLevel)
{
	//"%Y-%m-%d:%H:%M:%S.log"
	auto DailyFileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(pFileName, nHour, nMinute);
	DailyFileSink->set_level((spdlog::level::level_enum)eLevel);
	UpdateSinkMap(pLoggerName, DailyFileSink);
	return true;
}

bool Hlog::InitConfig(const char* pFileName)
{
	try
	{
		//从配置文件读取数据
		ifstream inf;
		inf.open(pFileName, ios::in);
		if (!inf.is_open())
			return false;
		json jsonRoot;
		inf >> jsonRoot;
		inf.close();
		std::string str = jsonRoot.dump();
		cout << str << endl;

		//解析数据
		json jsLoggers = GetJsonValue<json>("loggers", jsonRoot);
		string strOutMode = GetJsonValue<string>("outputMode", jsonRoot);
		string strOutFormat = GetJsonValue<string>("outputFormat", jsonRoot);
		OutMode eOutMode = GetOutModeEnum(strOutMode);
		//记录器
		for (int i = 0; i < (int)jsLoggers.size(); i++)
		{
			json jsLogger = jsLoggers[i];
			string strLoggerName = GetJsonValue<std::string>("name", jsLogger);
			json jsSinks = GetJsonValue<json>("sinks", jsLogger);

			//解析记录器里的所有sink
			for (int j = 0; j < (int)jsSinks.size(); j++)
			{
				json jsSink = jsSinks[j];
				string strType = GetJsonValue<std::string>("type", jsSink);
				string strLevel = GetJsonValue<std::string>("level", jsSink);
				Hlog::OutLevel eLevel = Hlog::GetOutLevelEnum(strLevel);
				if (strType == "color_console")//控制台
				{
					AddColorConsole(strLoggerName.c_str(), eLevel);
				}
				else if (strType == "rotating_file")//旋转文件
				{
					string strFileName = GetJsonValue<string>("fileName", jsSink);
					int nMaxFileSize = GetJsonValue<int>("maxFileSize", jsSink);
					int nMaxFile = GetJsonValue<int>("maxFile", jsSink);
					AddRotatingFile(strLoggerName.c_str(), strFileName.c_str(), nMaxFileSize, nMaxFile, eLevel);
				}
				else if (strType == "daily_file")//日期文件
				{
					string strFileName = GetJsonValue<string>("fileName", jsSink);
					int nHour = GetJsonValue<int>("hour", jsSink);
					int nMinute = GetJsonValue<int>("minute", jsSink);
					AddDailyFile(strLoggerName.c_str(), strFileName.c_str(), nHour, nMinute, eLevel);
				}
			}
		}

		return Init(eOutMode, strOutFormat);
	}
	catch (std::exception& e)
	{
		printf("AddConfig failed!, err:%s\n", e.what());
		return false;
	}
	catch (...)
	{
		printf("AddConfig failed!, exception\n");
		return false;
	}
	return true;
}

bool Hlog::Init(const OutMode outMode, const string strLogFormat)
{
	if (m_bInit)
	{
		printf("It's already initialized\n");
		return false;
	}
	m_bInit = true;

	if (outMode == ASYNC)//异步
	{
		printf("[log]: mode=ASYNC \n");
		for (auto e : m_mapLoggerParam)
		{
			std::string strLogName = e.first;
			std::vector<spdlog::sink_ptr> vecSink(e.second);
			auto tp = std::make_shared<spdlog::details::thread_pool>(1024000, 1);
			auto logger = std::make_shared<spdlog::async_logger>(strLogName, begin(vecSink), end(vecSink), tp, spdlog::async_overflow_policy::block);
			UpdateThreadPoolMap(strLogName, tp);
			//设置根日志输出等级
			logger->set_level(spdlog::level::trace);
			//遇到warn级别，立即flush到文件
			logger->flush_on(spdlog::level::warn);
			spdlog::register_logger(logger);
		}
	}
	else//同步
	{
		printf("[log]:  mode=SYNC \n");
		for (auto e : m_mapLoggerParam)
		{
			std::string strLogName = e.first;
			std::vector<spdlog::sink_ptr> vecSink(e.second);
			auto logger = std::make_shared<spdlog::logger>(strLogName, begin(vecSink), end(vecSink));
			//设置根日志输出等级
			logger->set_level(spdlog::level::trace);
			//遇到warn级别，立即flush到文件
			logger->flush_on(spdlog::level::warn);
			spdlog::register_logger(logger);
		}
	}

	//定时flush到文件，每三秒刷新一次
	spdlog::flush_every(std::chrono::seconds(3));
	//设置全局记录器的输出格式
	spdlog::set_pattern(strLogFormat);

	return true;
}

void Hlog::UnInit()
{
	spdlog::drop_all();
	spdlog::shutdown();
}

std::vector<std::string> Hlog::StringSplit(const std::string& strSrc, const std::string& strSplit)
{
	std::vector<std::string> resVec;
	if ("" == strSrc)
	{
		return resVec;
	}
	//方便截取最后一段数据
	std::string strs = strSrc + strSplit;

	size_t pos = strs.find(strSplit);
	size_t size = strs.size();

	while (pos != std::string::npos)
	{
		std::string x = strs.substr(0, pos);
		resVec.push_back(x);
		strs = strs.substr(pos + 1, size);
		pos = strs.find(strSplit);
	}

	return resVec;

}

void Hlog::UpdateSinkMap(std::string strLoggerName, spdlog::sink_ptr pSink)
{
	auto iter = m_mapLoggerParam.find(strLoggerName);
	if (iter != m_mapLoggerParam.end())
	{
		iter->second.push_back(pSink);
	}
	else
	{
		std::vector<spdlog::sink_ptr> vecSink;
		vecSink.push_back(pSink);
		m_mapLoggerParam[strLoggerName] = vecSink;
	}
}

void Hlog::UpdateThreadPoolMap(std::string strLoggerName, std::shared_ptr<spdlog::details::thread_pool> pThreadPool)
{
	auto iter = m_mapAsyncThreadPool.find(strLoggerName);
	if (iter != m_mapAsyncThreadPool.end())
	{
		iter->second = (pThreadPool);
	}
	else
	{
		m_mapAsyncThreadPool[strLoggerName] = pThreadPool;
	}
}