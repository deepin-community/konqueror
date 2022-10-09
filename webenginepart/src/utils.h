#ifndef WEBENGINEPART_UTILS_H
#define WEBENGINEPART_UTILS_H

#include <QUrl>

namespace Utils
{

#define QL1S(x)  QLatin1String(x)
#define QL1C(x)  QLatin1Char(x)

inline bool isBlankUrl(const QUrl& url)
{
    return (url.isEmpty() || url.url() == QL1S("konq:blank"));
}

inline bool isKonqUrl(const QUrl &url){
    return (url.scheme()) == QL1S("konq");
}

}
#endif // WEBENGINEPART_UTILS_H
