import Link from "next/link";
import { DiscFace, SOLID_CELL } from "@/components/discs";
import styles from "./not-found.module.css";

const CELL_COUNT = 49;
const LEFT_FOUR = 44;
const GRAY_ZERO = 45;
const DROP_COLUMN = 4;

const DESTINATIONS = [
  {
    href: "/research",
    label: "Research",
    description: "Theories, experiments, and results",
  },
  {
    href: "/learn",
    label: "Learn",
    description: "Rules, concepts, and strategy guides",
  },
  {
    href: "/leaderboard",
    label: "Leaderboard",
    description: "The scripted-round playground",
  },
  {
    href: "/compete",
    label: "Compete",
    description: "Submit a run and join the challenge",
  },
] as const;

function FourOhFourBoard() {
  return (
    <figure className={styles.figure}>
      <div
        className={styles.boardScene}
        role="img"
        aria-label="A Drop7 board spelling 404 with a red 4, a gray disc, and a second red 4 dropping into place"
      >
        <div className={styles.boardGlow} aria-hidden="true" />
        <div className={styles.boardFrame} aria-hidden="true">
          <div className={styles.board}>
            {Array.from({ length: CELL_COUNT }, (_, index) => {
              const isDropColumn = index % 7 === DROP_COLUMN;

              return (
                <span
                  key={index}
                  className={`${styles.cell} ${
                    isDropColumn ? styles.dropColumn : ""
                  } ${index === 46 ? styles.landingCell : ""}`}
                >
                  {index === LEFT_FOUR ? (
                    <DiscFace cell={4} className={styles.disc} />
                  ) : null}
                  {index === GRAY_ZERO ? (
                    <DiscFace cell={SOLID_CELL} className={styles.disc} />
                  ) : null}
                </span>
              );
            })}

            <span className={styles.fallingLayer}>
              <span className={styles.fallingCell}>
                <DiscFace cell={4} className={styles.disc} />
              </span>
            </span>
          </div>
        </div>
      </div>
    </figure>
  );
}

export default function NotFound() {
  return (
    <section className={styles.page} aria-labelledby="not-found-title">
      <div className={styles.shell}>
        <FourOhFourBoard />

        <div className={styles.copy}>
          <p className={styles.eyebrow}>404 · not found</p>
          <h1 id="not-found-title" className={styles.title}>
            That&apos;s not a legal move.
          </h1>
          <p className={styles.intro}>
            You can{" "}
            <Link
              className="text-accent underline underline-offset-2 hover:text-ink"
              href="https://github.com/keshavsaharia/drop7-bench/issues"
            >
              create an issue
            </Link>{" "}
            on GitHub if you expected something else.
          </p>

          <div className={styles.actions}>
            <Link href="/" className={styles.primaryAction}>
              Back to home <span aria-hidden="true">→</span>
            </Link>
            <Link href="/play" className={styles.secondaryAction}>
              Play Drop7
            </Link>
          </div>

          <nav aria-label="Explore the Drop7 app" className={styles.destinations}>
            {DESTINATIONS.map((destination) => (
              <Link
                key={destination.href}
                href={destination.href}
                className={styles.destination}
              >
                <span>
                  <strong>{destination.label}</strong>
                  <small>{destination.description}</small>
                </span>
                <span className={styles.destinationArrow} aria-hidden="true">
                  ↗
                </span>
              </Link>
            ))}
          </nav>
        </div>
      </div>
    </section>
  );
}
