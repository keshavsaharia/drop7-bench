/**
 * The "Source data" table: the accessibility twin of every chart kind. Its
 * columns come from web/lib/charts/spec.ts specTable, so the table and the
 * chart list exactly the same recorded values. `links` maps a source id to
 * the console route that opens it (Figure resolves result records to their
 * experiment page on the server). Server-safe; no hooks.
 */
import { specTable, type FigureSpec } from "@/lib/charts/spec";
import { SourceRef } from "./SourceRef";

export function TableView({ spec, links, id }: { spec: FigureSpec; links?: Record<string, string>; id?: string }) {
  const table = specTable(spec);
  return (
    <div className="rchart-data-scroll" id={id}>
      <table className="rchart-data-table">
        <thead>
          <tr>
            {table.columns.map((column) => (
              <th key={column.label} className={column.numeric ? "num" : undefined} scope="col">
                {column.label}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {table.rows.map((row, ri) => (
            <tr key={ri}>
              {row.map((cell, ci) => (
                <td key={ci} className={table.columns[ci]?.numeric ? "num" : undefined}>
                  {cell.source ? (
                    <SourceRef id={cell.source} field={cell.field} href={links?.[cell.source]} />
                  ) : (
                    <>
                      {cell.text}
                      {cell.note && (
                        <span className="rchart-data-note" title={cell.note}>
                          {" "}
                          {cell.note}
                        </span>
                      )}
                    </>
                  )}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
