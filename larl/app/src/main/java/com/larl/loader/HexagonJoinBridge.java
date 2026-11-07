package com.larl.loader;

import android.net.Uri;
import android.webkit.CookieManager;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/** Reproduces the legacy WebView handoff for games/start?placeid=. */
public final class HexagonJoinBridge extends WebViewClient {
    public interface Listener {
        void onJoinRequested(JoinRequest request);
        void onJoinError(String message);
    }

    public static final class JoinRequest {
        public final int placeId;
        public final int userId;
        public final String accessCode;
        public final String gameId;
        public final int requestType;

        JoinRequest(int placeId, int userId, String accessCode, String gameId, int requestType) {
            this.placeId = placeId;
            this.userId = userId;
            this.accessCode = accessCode;
            this.gameId = gameId;
            this.requestType = requestType;
        }
    }

    private final Listener listener;
    private boolean consumed;

    public HexagonJoinBridge(Listener listener) {
        this.listener = listener;
    }

    public static void configureCookie(String baseUrl, String token) {
        CookieManager manager = CookieManager.getInstance();
        manager.setAcceptCookie(true);
        manager.setCookie(baseUrl, ".ROBLOSECURITY=" + token + "; Path=/");
        manager.flush();
    }

    @Override
    public boolean shouldOverrideUrlLoading(WebView view, String url) {
        return consume(url);
    }

    /** The legacy frontend explicitly routes its initial games/start URL through this handler. */
    public boolean startUrl(String url) {
        return consume(url);
    }

    private boolean consume(String url) {
        Uri uri = Uri.parse(url);
        if (!uri.getPath().contains("/games/start")) {
            return false;
        }
        if (consumed) {
            return true;
        }
        String place = uri.getQueryParameter("placeid");
        if (place == null) {
            listener.onJoinError("Hexagon start URL did not provide placeid");
            return true;
        }
        try {
            int placeId = Integer.parseInt(place);
            String user = uri.getQueryParameter("userID");
            String accessCode = uri.getQueryParameter("accessCode");
            String gameId = uri.getQueryParameter("gameInstanceId");
            // In the legacy frontend request type 1 is only used when there is no
            // placeid and the URL names a user to follow.  A normal placeid launch is
            // type 0; treating it as type 1 produces RequestFollowUser&userId=0.
            int requestType = accessCode != null ? 2 : gameId != null ? 3 : 0;
            consumed = true;
            listener.onJoinRequested(new JoinRequest(placeId, user == null ? 0 : Integer.parseInt(user),
                    accessCode, gameId, requestType));
        } catch (NumberFormatException error) {
            listener.onJoinError("Invalid Hexagon join values: " + error.getMessage());
        }
        return true;
    }
}
