#include "handlers/include/AuthHandler.hpp"

AuthHandler::AuthHandler(const AuthCommands& command)
    : command(command) {}

AuthHandler::~AuthHandler() = default;

void AuthHandler::handleRequest(Poco::Net::HTTPServerRequest &request, Poco::Net::HTTPServerResponse &response)
{
    response.set("Access-Control-Allow-Origin", CORS);
    response.set("Access-Control-Allow-Method", "GET, POST");

    try
    {
        if ((request.getMethod() == Poco::Net::HTTPRequest::HTTP_GET) ||
            (request.getMethod() == Poco::Net::HTTPRequest::HTTP_POST))
        {
            std::unique_ptr<AuthMethods> method(new AuthMethods());
            std::string resp_string;

            auto json = getJson(request);

            switch (command)
            {
                case AuthCommands::SIGN_UP:
                {
                    method->signUp(
                        getField(json, FrontData::username),
                        getField(json, FrontData::password),
                        getField(json, FrontData::mail));

                    break;
                }
                case AuthCommands::SIGN_UP_VERIFY_URL:
                {
                    method->signUpVerify(
                        getUrlData(request.getURI(), SignUpVerifyURL));

                    break;
                }
                case AuthCommands::SIGN_IN:
                {
                    resp_string = method->signIn(
                        getField(json, FrontData::username),
                        getField(json, FrontData::password));

                    break;
                }
                case AuthCommands::SIGN_OUT:
                {
                    method->signOut(
                        getHeader(request, FrontData::token));

                    break;
                }
                case AuthCommands::REFRESH:
                {
                    resp_string = method->refresh(
                        getHeader(request, FrontData::token));

                    break;
                }
                case AuthCommands::MAIL_PASSWORD_RECOVERY:
                {
                    method->mailPasswordRecovery(
                        getField(json, FrontData::mail));

                    break;
                }
                case AuthCommands::CHECK_RECOVERY_TOKEN:
                {
                    resp_string = method->checkRecoveryToken(
                        getUrlData(request.getURI(), CheckRecoveryTokenURL));

                    break;
                }
                case AuthCommands::PASSWORD_RECOVERY:
                {
                    method->passwordRecovery(
                        getTokenUsername(request),
                        getField(json, FrontData::username),
                        getField(json, FrontData::password));

                    break;
                }
                default:
                    break;
            }

            if(!resp_string.empty())
            {
                response.setStatus(Poco::Net::HTTPResponse::HTTPStatus::HTTP_OK);
                Poco::Net::MediaType type("application/json");
                response.setContentType(type);
                std::ostream &out = response.send();
                out << resp_string;
            }
            else
            {
                response.setStatus(Poco::Net::HTTPResponse::HTTPStatus::HTTP_NO_CONTENT);
                response.send();
            }

            printLogs(request, response);
        }
        else if (request.getMethod() == Poco::Net::HTTPRequest::HTTP_OPTIONS)
        {
            response.setStatus(Poco::Net::HTTPResponse::HTTPStatus::HTTP_OK);
            response.set("Access-Control-Allow-Headers", "token, Content-Type Accept");
            response.set("Access-Control-Request-Headers", "token, Content-Type Accept");
            response.send();

            printLogs(request, response);
        }
        else
        {
            throw Poco::Net::HTTPException("HTTP_METHOD_NOT_ALLOWED");
        }
    }
    catch (const Poco::InvalidArgumentException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_NOT_FOUND;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::Net::NotAuthenticatedException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_UNAUTHORIZED;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::InvalidAccessException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_FORBIDDEN;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::NotFoundException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_NOT_FOUND;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::ApplicationException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_INTERNAL_SERVER_ERROR;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::Net::ConnectionRefusedException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_BAD_GATEWAY;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::Redis::RedisException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_INTERNAL_SERVER_ERROR;
        sendResponse(request, response, status, ex.message());
    }
    catch (const Poco::Net::HTTPException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_METHOD_NOT_ALLOWED;
        sendResponse(request, response, status, ex.message());
    }
    catch (Poco::Net::SMTPException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_BAD_GATEWAY;
        sendResponse(request, response, status, ex.message());
    }
    catch (Poco::Net::NetException &ex)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_BAD_GATEWAY;
        sendResponse(request, response, status, ex.message());
    }
    catch (...)
    {
        const int status = Poco::Net::HTTPResponse::HTTPStatus::HTTP_INTERNAL_SERVER_ERROR;
        sendResponse(request, response, status, "HTTP_INTERNAL_SERVER_ERROR");
    }
}

boost::property_tree::ptree AuthHandler::getJson(Poco::Net::HTTPServerRequest &request)
{
    boost::property_tree::ptree json;
    std::string data;
    auto& stream = request.stream();
    getline(stream, data);
    std::stringstream ss;
    ss << data;
    if (!data.empty())
    {
        boost::property_tree::read_json(ss, json);
    }

    return json;
}

std::string AuthHandler::getHeader(Poco::Net::HTTPServerRequest& request, const std::string& header)
{
    if (!request.has(header))
    {
        throw Poco::InvalidArgumentException("'" + header + "' header is missing");
    }

    const std::string data = request.get(header);

    return data;
}

std::string AuthHandler::getField(const boost::property_tree::ptree& json, const std::string& field)
{
    std::string data;

    for (const auto &it : json)
    {
        if (it.first == field)
        {
            data = it.second.get_value<std::string>();

            if (data.empty())
            {
                throw Poco::InvalidArgumentException("Поле '" + field + "' не заполнено");
            }
        }
    }

    if (data.empty())
    {
        throw Poco::InvalidArgumentException("'" + field + "' field is missing");
    }

    return data;
}

std::string AuthHandler::getUrlData(const std::string& now_url, const std::string& valid_url)
{
    std::string data = now_url.substr(valid_url.size(), now_url.size());

    return data;
}

std::string AuthHandler::getTokenUsername(Poco::Net::HTTPServerRequest& request)
{
    if (!request.has(FrontData::token))
    {
        throw Poco::InvalidArgumentException("'" + FrontData::token + "' header is missing");
    }

    const std::string recoveryToken = request.get(FrontData::token);

    std::string username = Auth::checkRecoveryToken(recoveryToken);

    return username;
}