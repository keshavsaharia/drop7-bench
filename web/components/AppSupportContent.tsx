import Link from "next/link";

export function AppSupportContent() {
  return (
    <>
      <h2 id="contact">Contact support</h2>
      <p>
        For help with the Drop7 Research app, bug reports, feedback, or feature requests,
        email <a href="mailto:support@drop7.dev">support@drop7.dev</a>.
      </p>
      <p>
        Include your device model, iOS version, app version, and what happened. If the
        problem involves a game, include the mode, score, and the steps that led to it.
        A screenshot can help us understand the issue.
      </p>

      <h2 id="troubleshooting">Troubleshooting</h2>
      <p>
        If the app stops responding, close and reopen it, then check the App Store for
        an update. If Compete cannot load or a leaderboard submission fails, check your
        internet connection and try again. Classic and Hardcore can be played offline.
      </p>
      <p>
        Contact us before deleting the app to troubleshoot a problem. Game history is
        stored on your device, and deleting the app can remove saved games and replays.
      </p>

      <h2 id="playing">Playing Drop7</h2>
      <p>
        Drop numbered discs into the grid. A disc clears when its number matches the
        count of connected discs in its row or column. Falling discs can trigger
        another clear, building a chain reaction. Clears beside covered discs crack
        them, then reveal their numbers.
      </p>
      <p>
        Open How to play in the app menu for an interactive walkthrough, or read the{" "}
        <Link href="/learn/rules">game rules</Link>. Classic starts with more moves
        between rising rows and gradually becomes harder. Hardcore adds a covered row
        after every five drops.
      </p>

      <h2 id="history">Game history and replays</h2>
      <p>
        Open Game history from the game menu and select a completed game to watch its
        replay. History is organized by mode. Replays let you step through moves and
        revisit the chain reactions that built your score.
      </p>
      <p>
        Saved history stays on the device where you played. There is no account-based
        history sync between devices. If a game seems to be missing, check the same
        mode on the same device before contacting support.
      </p>

      <h2 id="competitions">Competitions and leaderboards</h2>
      <p>
        Compete gives players the same starting board and disc sequence for each
        competition. An internet connection is needed to load competitions and submit
        scores. After finishing a run in the app, you can choose a display name and
        submit it to the public leaderboard without creating an account.
      </p>
      <p>
        Leaderboard submission is optional. Your submitted display name and game can
        appear publicly. For help with an entry, email its display name, competition,
        score, and approximate submission time.
      </p>

      <h2 id="settings">Settings and purchases</h2>
      <p>
        You can turn haptic feedback on or off in the app menu. The app has no ads,
        in-app purchases, or subscriptions, and you do not need an account to play.
      </p>

      <h2 id="privacy">Privacy and game data</h2>
      <p>
        The app sends interaction events and completed game records to the Drop7
        Research project. Game records include the discs and moves needed to replay
        a game, and failed deliveries are retried when a connection is available.
        These records do not include an account or device identifier.
      </p>
      <p>
        Read the <Link href="/privacy">privacy policy</Link> for details about data use
        and retention, and the <Link href="/terms">terms of service</Link> for use of
        the app and website. For privacy questions or requests about submitted data,
        email <a href="mailto:support@drop7.dev">support@drop7.dev</a>.
      </p>
    </>
  );
}
