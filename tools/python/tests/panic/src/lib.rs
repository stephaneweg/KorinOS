//! Essai de l'étape 5 (docs/PYTHON-PORT.md) : un `panic!` dans une cdylib pyo3 chargée par dlopen
//! doit devenir une `PanicException` Python (le dérouleur de l'hôte, `dlunwind.S` de libposix),
//! pas un `abort` du process.
use pyo3::prelude::*;

#[pyfunction]
fn panic(msg: &str) -> PyResult<()> {
    panic!("panique voulue : {msg}");
}

/// Une panique rattrapée dans le module lui-même, puis une valeur rendue.
#[pyfunction]
fn caught() -> String {
    let r = std::panic::catch_unwind(|| panic!("dedans"));
    format!("rattrapée : {}", r.is_err())
}

#[pymodule]
fn korin_panic_probe(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(panic, m)?)?;
    m.add_function(wrap_pyfunction!(caught, m)?)?;
    Ok(())
}
