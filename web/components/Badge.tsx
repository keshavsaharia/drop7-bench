export function Badge({ label, className }: { label: string; className?: string }) {
  return (
    <span
      className={`inline-block rounded px-1.5 py-0.5 text-[11px] font-medium ${
        className ?? "bg-zinc-800 text-zinc-300"
      }`}
    >
      {label}
    </span>
  );
}
