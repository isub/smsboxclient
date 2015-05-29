#ifdef WIN32
#	include <Windows.h>
#endif
#include "utils/config/config.h"
#include "utils/dbpool/dbpool.h"
#include "utils/log/log.h"
#include <curl/curl.h>
#include <errno.h>
#include <iconv.h>

int is_ascii_string(
	const char *p_pszString,
	size_t p_stStrLen);
int conv_string_to_ucs2(
	CLog &p_coLog,
	otl_connect &p_coDBConn,
	std::string &p_strIn,
	std::string &p_strOut);
int put_sms(
	otl_connect &p_coDBConn,
	CLog &p_coLog,
	std::string p_strHost,
	std::string p_strURL,
	std::string p_strUserName,
	std::string p_strUserPswd,
	std::string p_strFrom,
	std::string p_strTo,
	std::string p_strText);
void append_urlparam(
	CURL *pCurl,
	std::string &p_strParamSet,
	const char *p_pszParamName,
	std::string &p_strParam,
	bool &p_bFirstParam);

int main(int argc, char *argv[])
{
	int iRetVal = 0;
	int iFnRes;
	CLog coLog;
	CConfig coConf;
	std::string strConfParamValue;
	std::string strSMSBoxUserName;
	std::string strSMSBoxUserPswd;
	std::string strSMSBoxHost;
	std::string strSMSBoxURL;
	const char *pszParamName = NULL;

	/* проверяем наличие параметра в командной строке */
	if (argc < 2) {
		iRetVal = -1;
		LOG_E(coLog, "return code: '%d';", iRetVal);
		return iRetVal;
	}
	/* загружаем конфигурацию */
	iFnRes = coConf.LoadConf(argv[1]);
	if (iFnRes) {
		iRetVal = -2;
		LOG_E(coLog, "return code: '%d';", iRetVal);
		return iRetVal;
	}
	/* инициализируем логгер */
	iFnRes = coConf.GetParamValue("log_file_mask", strConfParamValue);
	if (iFnRes) {
		iRetVal = -3;
		LOG_E(coLog, "return code: '%d';", iRetVal);
		return iRetVal;
	}
	iFnRes = coLog.Init(strConfParamValue.c_str());
	if (iFnRes) {
		iRetVal = -4;
		LOG_E(coLog, "return code: '%d';", iRetVal);
		return iRetVal;
	}
	/* инициализация сurl */
	curl_global_init(0);
	/* инициализируем пул подключений к БД */
	iFnRes = db_pool_init(&coLog, &coConf);
	if (iFnRes) {
		iRetVal = -5;
		LOG_E(coLog, "return code: '%d';", iRetVal);
		return iRetVal;
	}

	otl_stream coStream;
	otl_stream coUpdateRow;
	otl_connect *pcoDBConn = NULL;
	pcoDBConn = db_pool_get();

	/* запрашиваем необходимые параметры */
	pszParamName = "smsbox_username";
	iFnRes = coConf.GetParamValue(pszParamName, strSMSBoxUserName);
	if (iFnRes) {
		LOG_W(coLog, "parameter '%s' is not defined in configuration", pszParamName);
	}
	pszParamName = "smsbox_userpswd";
	iFnRes = coConf.GetParamValue(pszParamName, strSMSBoxUserPswd);
	if (iFnRes) {
		LOG_W(coLog, "parameter '%s' is not defined in configuration", pszParamName);
	}
	pszParamName = "smsbox_host";
	iFnRes = coConf.GetParamValue(pszParamName, strSMSBoxHost);
	if (iFnRes) {
		LOG_W(coLog, "parameter '%s' is not defined in configuration", pszParamName);
	}
	pszParamName = "smsbox_url";
	iFnRes = coConf.GetParamValue(pszParamName, strSMSBoxURL);
	if (iFnRes) {
		LOG_W(coLog, "parameter '%s' is not defined in configuration", pszParamName);
	}

	if (pcoDBConn) {
		try {
			otl_value<std::string> coHeader;
			otl_value<std::string> coTo;
			otl_value<std::string> coText;
			otl_value<std::string> coRowId;
			coStream.open(50, "select rowid, header, msisdn, message from ps.smsqueue where status = 0", *pcoDBConn);
			coUpdateRow.open(1, "update ps.smsqueue set status = :status/*int*/, sent = sysdate where rowid = :row_id/*char[256]*/", *pcoDBConn);
			while (!coStream.eof()) {
				coStream
					>> coRowId
					>> coHeader
					>> coTo
					>> coText;
				if (coHeader.is_null()) coHeader.v = "";
				if (coTo.is_null()) {
					coTo.v = "";
				} else {
					if (coTo.v[0] != '+') coTo.v = '+' + coTo.v;
				}
				if (coText.is_null()) coText.v = "";
				iFnRes = put_sms(
					*pcoDBConn,
					coLog,
					strSMSBoxHost,
					strSMSBoxURL,
					strSMSBoxUserName,
					strSMSBoxUserPswd,
					coHeader.v,
					coTo.v,
					coText.v);
				coUpdateRow
					<< iFnRes
					<< coRowId;
				pcoDBConn->commit();
				LOG_N(coLog, "sms is sent with status '%d': '%s'; '%s'; '%s';", iFnRes, coHeader.v.c_str(), coTo.v.c_str(), coText.v.c_str());
			}
			coStream.close();
			coUpdateRow.close();
		} catch (otl_exception &coExept) {
			LOG_E(coLog, "code: '%d'; message: '%s'; query: '%s';", coExept.code, coExept.msg, coExept.stm_text);
			if (coStream.good())
				coStream.close();
			if (coUpdateRow.good())
				coUpdateRow.close();
		}
	} else {
		LOG_E(coLog, "can't get DB connection");
	}

	if (pcoDBConn) {
		db_pool_release(pcoDBConn);
	}

	/* очищаем CURL */
	curl_global_cleanup();
	/* освобождаем пул подключений к БД */
	db_pool_deinit();

	return iRetVal;
}

int put_sms(
	otl_connect &p_coDBConn,
	CLog &p_coLog,
	std::string p_strHost,
	std::string p_strURL,
	std::string p_strUserName,
	std::string p_strUserPswd,
	std::string p_strFrom,
	std::string p_strTo,
	std::string p_strText)
{
	int iRetVal = 0;
	int iFnRes;

	/* инициализация дескриптора */
	CURL *pCurl = curl_easy_init();
	if (NULL == pCurl) {
		return ENOMEM;
	}

	char mcError[CURL_ERROR_SIZE];
	curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, mcError);

	/* проверяем кодировку строки */
	std::string strText;
	std::string strCoding;
	if (!is_ascii_string(p_strText.data(), p_strText.length())) {
		iFnRes = conv_string_to_ucs2(p_coLog, p_coDBConn, p_strText, strText);
		strCoding = '2';
	} else {
		strText = p_strText;
		strCoding = '0';
	}

	do {
		CURLcode curlCode;
		curl_slist *psoList = NULL;
		std::string strHeader;
		/* формируем необходимые HTTP-заголовки */
		/* добавляем в заголовки Host */
		strHeader = "Host: " + p_strHost;
		psoList = curl_slist_append(psoList, strHeader.c_str());
		/* добавляем в заголовки Connection */
		strHeader = "Connection: close";
		psoList = curl_slist_append(psoList, strHeader.c_str());
		/* добавляем список заголовков в HTTP-запрос */
		curlCode = curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, psoList);
		if (curlCode) {
			LOG_E(p_coLog, "can't set option '%s'; error: code: '%d'; description: '%s';", "CURLOPT_HTTPHEADER", curlCode, mcError);
			iRetVal = -100;
			break;
		}
		/* задаем User-Agent */
		curlCode = curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "smsboxclient/0.1");
		if (curlCode) {
			LOG_E(p_coLog, "can't set option '%s'; error: code: '%d'; description: '%s';", "CURLOPT_USERAGENT", curlCode, mcError);
			iRetVal = -101;
			break;
		}
		/* формируем URL */
		bool bFirst = true;
		std::string strURL;
		std::string strParam;
		strURL += p_strHost;
		strURL += p_strURL;
		append_urlparam(pCurl, strParam, "username", p_strUserName, bFirst);
		append_urlparam(pCurl, strParam, "password", p_strUserPswd, bFirst);
		append_urlparam(pCurl, strParam, "from", p_strFrom, bFirst);
		append_urlparam(pCurl, strParam, "to", p_strTo, bFirst);
		append_urlparam(pCurl, strParam, "text", strText, bFirst);
		append_urlparam(pCurl, strParam, "coding", strCoding, bFirst);
		strURL += strParam;
		curlCode = curl_easy_setopt(pCurl, CURLOPT_URL, strURL.c_str());
		if (curlCode) {
			LOG_E(p_coLog, "can't set option '%s'; error: code: '%d'; description: '%s';", "CURLOPT_URL", curlCode, mcError);
			iRetVal = -103;
			break;
		}
		/* задаем тип запроса GET */
		curlCode = curl_easy_setopt(pCurl, CURLOPT_HTTPGET, 1L);
		if (curlCode) {
			LOG_E(p_coLog, "can't set option '%s'; error: code: '%d'; description: '%s';", "CURLOPT_HTTPGET", curlCode, mcError);
			iRetVal = -104;
			break;
		}
		LOG_N(p_coLog, "try to send request: '%s';", strURL.c_str());
		curlCode = curl_easy_perform(pCurl);
		if (curlCode) {
			LOG_E(p_coLog, "can't execute request: error code: '%d'; description: '%s'; url: '%s';", curlCode, mcError, strURL.c_str());
			iRetVal = -105;
			break;
		}
		long lResulCode;
		curlCode = curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &lResulCode);
		if (curlCode) {
			LOG_E(p_coLog, "can't retrieve request result code: error code: '%d'; description: '%s';", curlCode, mcError);
			iRetVal = -106;
			break;
		}
		LOG_N(p_coLog, "request is sent successfully: response code: '%u';", lResulCode);
		/* операция завершена успешно */
		iRetVal = lResulCode;
	} while (0);

	/* очистка дескриптора */
	if (pCurl) {
		curl_easy_cleanup(pCurl);
	}

	return iRetVal;
}

void append_urlparam(
	CURL *pCurl,
	std::string &p_strParamSet,
	const char *p_pszParamName,
	std::string &p_strParamVal,
	bool &p_bFirstParam)
{
	char *pszEncodedString;
	if (p_bFirstParam) {
		p_strParamSet += '?';
		p_bFirstParam = false;
	} else {
		p_strParamSet += '&';
	}
	if (p_pszParamName) {
		pszEncodedString = curl_easy_escape(pCurl, p_pszParamName, 0);
		if (pszEncodedString) {
			p_strParamSet += pszEncodedString;
			p_strParamSet += '=';
			curl_free(pszEncodedString);
		}
	}
	if (p_strParamVal.length()) {
		pszEncodedString = curl_easy_escape(pCurl, p_strParamVal.data(), p_strParamVal.length());
		if (pszEncodedString) {
			p_strParamSet += pszEncodedString;
			curl_free(pszEncodedString);
		}
	}
}

int is_ascii_string(
	const char *p_pszString,
	size_t p_stStrLen)
{
	int iRetVal = 1;

	for (size_t i = 0; i < p_stStrLen; ++i) {
		if ((unsigned int) p_pszString[i] > 127) {
			iRetVal = 0;
			break;
		}
	}

	return iRetVal;
}

int conv_string_to_ucs2(
	CLog &p_coLog,
	otl_connect &p_coDBConn,
	std::string &p_strString,
	std::string &p_strOut)
{
	int iRetVal = 0;

	/* запрашиваем в БД текущую кодировку символов */
	char mcDBCharset[64];
	otl_stream coStream;
	try {
		coStream.open(1, "select value from NLS_DATABASE_PARAMETERS where parameter='NLS_CHARACTERSET'", p_coDBConn);
		if (coStream.eof()) {
			iRetVal = -1;
			LOG_E(p_coLog, "empty dataset;");
		} else {
			coStream
				>> mcDBCharset;
		}
	} catch (otl_exception coExcept) {
		iRetVal = coExcept.code;
		LOG_E(p_coLog, "DB code: '%d'; message: '%s'; query: '%s';", coExcept.code, coExcept.msg, coExcept.stm_text);
	}

	if (iRetVal) {
		return iRetVal;
	}

	const char *pszCSFrom;
	if (0 == strcmp(mcDBCharset, "CL8ISO8859P5")) {
		pszCSFrom = "ISO-8859-5";
	} else {
		pszCSFrom = NULL;
		LOG_E(p_coLog, "unsupporterd charset: '%s';", mcDBCharset);
	}

	if (NULL == pszCSFrom) {
		return -200;
	}

	iconv_t tIConv;

	tIConv = iconv_open("UCS-2BE", pszCSFrom);
	if (tIConv == (iconv_t)-1) {
		iRetVal = errno;
		LOG_E(p_coLog, "can't create iconv descriptor; error: code: '%d';", iRetVal);
		return iRetVal;
	}

	size_t stStrLen;
	const char *pszInBuf;
	char *pszResult;
	size_t stResultSize;
	char *pszOut;

	pszInBuf = p_strString.data();
	stStrLen = p_strString.length();
	stResultSize = stStrLen * 4;

	pszResult = (char*)malloc(stResultSize);
	pszOut = pszResult;
	stStrLen = iconv(tIConv, (char**)(&pszInBuf), &stStrLen, &pszOut, &stResultSize);
	if (stStrLen == (size_t)-1) {
		iRetVal = errno;
		LOG_E(p_coLog, "iconv conversion error: code: '%d';", iRetVal);
		goto clean_and_exit;
	}

	stStrLen = pszOut - pszResult;

	p_strOut.insert(0, pszResult, stStrLen);

clean_and_exit:

	iconv_close(tIConv);

	if (pszResult) {
		free(pszResult);
	}

	return iRetVal;
}
